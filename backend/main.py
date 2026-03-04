import asyncio
import json
import logging
import os
import threading
from contextlib import asynccontextmanager
from datetime import datetime
from typing import Optional

import paho.mqtt.client as mqtt
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse
from pydantic import BaseModel

from database import Database

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
logger = logging.getLogger(__name__)

# Configuration from environment
MQTT_HOST = os.getenv("MQTT_HOST", "localhost")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
MQTT_USER = os.getenv("MQTT_USER", "")
MQTT_PASS = os.getenv("MQTT_PASS", "")

db = Database()
mqtt_client: Optional[mqtt.Client] = None
main_loop: Optional[asyncio.AbstractEventLoop] = None
mqtt_queue: Optional[asyncio.Queue] = None


# ─── WebSocket Manager ────────────────────────────────────────────────────────

class ConnectionManager:
    def __init__(self):
        self.active: list[WebSocket] = []

    async def connect(self, ws: WebSocket):
        await ws.accept()
        self.active.append(ws)

    def disconnect(self, ws: WebSocket):
        if ws in self.active:
            self.active.remove(ws)

    async def broadcast(self, data: dict):
        dead = []
        for ws in self.active:
            try:
                await ws.send_json(data)
            except Exception:
                dead.append(ws)
        for ws in dead:
            self.active.remove(ws)


manager = ConnectionManager()


# ─── MQTT Callbacks ───────────────────────────────────────────────────────────

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        logger.info("Connected to MQTT broker")
        client.subscribe("home/devices/+/telemetry")
        client.subscribe("home/devices/+/status")
    else:
        logger.error(f"MQTT connection failed, rc={rc}")


def on_disconnect(client, userdata, rc):
    if rc != 0:
        logger.warning(f"Unexpected MQTT disconnect, rc={rc}")


def on_message(client, userdata, msg):
    if main_loop and mqtt_queue:
        try:
            payload_str = msg.payload.decode()
            try:
                payload = json.loads(payload_str)
            except json.JSONDecodeError:
                payload = payload_str
            asyncio.run_coroutine_threadsafe(
                mqtt_queue.put({"topic": msg.topic, "payload": payload}),
                main_loop,
            )
        except Exception as e:
            logger.error(f"on_message error: {e}")


# ─── MQTT Message Processor ───────────────────────────────────────────────────

async def process_mqtt_messages():
    while True:
        try:
            msg = await asyncio.wait_for(mqtt_queue.get(), timeout=1.0)
            await handle_mqtt_message(msg["topic"], msg["payload"])
        except asyncio.TimeoutError:
            continue
        except asyncio.CancelledError:
            break
        except Exception as e:
            logger.error(f"Error processing MQTT: {e}")


async def handle_mqtt_message(topic: str, payload):
    parts = topic.split("/")
    if len(parts) != 4 or parts[0] != "home" or parts[1] != "devices":
        return

    device_id = parts[2]
    msg_type = parts[3]
    now = datetime.now().isoformat()

    if msg_type == "telemetry" and isinstance(payload, dict):
        await db.update_device_telemetry(device_id, payload)
        await manager.broadcast({
            "type": "telemetry",
            "device_id": device_id,
            "data": payload,
            "timestamp": now,
        })

    elif msg_type == "status":
        status = payload if isinstance(payload, str) else payload.get("status", "unknown")
        await db.update_device_status(device_id, status)
        await manager.broadcast({
            "type": "status",
            "device_id": device_id,
            "status": status,
            "timestamp": now,
        })


# ─── App Lifecycle ────────────────────────────────────────────────────────────

@asynccontextmanager
async def lifespan(app: FastAPI):
    global main_loop, mqtt_queue, mqtt_client

    main_loop = asyncio.get_event_loop()
    mqtt_queue = asyncio.Queue()

    await db.init()
    await db.delete_old_records(days=30)
    logger.info("Database ready")

    client = mqtt.Client()
    if MQTT_USER:
        client.username_pw_set(MQTT_USER, MQTT_PASS)
    client.on_connect = on_connect
    client.on_message = on_message
    client.on_disconnect = on_disconnect

    try:
        client.connect(MQTT_HOST, MQTT_PORT, keepalive=60)
        client.loop_start()
        mqtt_client = client
        logger.info(f"MQTT connecting to {MQTT_HOST}:{MQTT_PORT}")
    except Exception as e:
        logger.warning(f"MQTT broker unavailable: {e}. Continuing without MQTT.")

    task = asyncio.create_task(process_mqtt_messages())

    yield

    task.cancel()
    try:
        await task
    except asyncio.CancelledError:
        pass
    if mqtt_client:
        mqtt_client.loop_stop()
    logger.info("Shutdown complete")


# ─── FastAPI App ──────────────────────────────────────────────────────────────

app = FastAPI(title="Domotique API", version="1.0.0", lifespan=lifespan)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


# ─── REST Endpoints ───────────────────────────────────────────────────────────

@app.get("/api/devices")
async def get_devices():
    return await db.get_devices()


@app.get("/api/devices/{device_id}/history/{sensor}")
async def get_history(device_id: str, sensor: str, hours: int = 24):
    return await db.get_history(device_id, sensor, hours)


class CommandRequest(BaseModel):
    relay: Optional[bool] = None
    display: Optional[str] = None
    custom: Optional[dict] = None


@app.post("/api/devices/{device_id}/command")
async def send_command(device_id: str, command: CommandRequest):
    if not mqtt_client:
        return JSONResponse(
            {"status": "error", "message": "MQTT broker non connecté"},
            status_code=503,
        )
    topic = f"home/devices/{device_id}/command"
    payload = json.dumps(command.model_dump(exclude_none=True))
    mqtt_client.publish(topic, payload)
    return {"status": "sent", "device_id": device_id, "command": command.model_dump(exclude_none=True)}


@app.get("/api/health")
async def health():
    return {
        "status": "ok",
        "mqtt_connected": mqtt_client is not None and mqtt_client.is_connected(),
        "ws_clients": len(manager.active),
    }


# ─── WebSocket Endpoint ───────────────────────────────────────────────────────

@app.websocket("/ws")
async def websocket_endpoint(ws: WebSocket):
    await manager.connect(ws)
    logger.info(f"WebSocket connected (total: {len(manager.active)})")
    try:
        # Send current device state on connect
        devices = await db.get_devices()
        await ws.send_json({"type": "init", "devices": devices})
        # Keep alive — client sends pings
        while True:
            await ws.receive_text()
    except WebSocketDisconnect:
        manager.disconnect(ws)
        logger.info("WebSocket disconnected")
