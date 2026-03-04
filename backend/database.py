import aiosqlite
import json
from datetime import datetime, timedelta
from typing import List, Dict

DB_PATH = "domotique.db"


class Database:
    def __init__(self):
        self._initialized = False

    async def init(self):
        if self._initialized:
            return
        async with aiosqlite.connect(DB_PATH) as db:
            await db.execute("""
                CREATE TABLE IF NOT EXISTS devices (
                    device_id TEXT PRIMARY KEY,
                    room TEXT,
                    status TEXT DEFAULT 'unknown',
                    last_seen TEXT,
                    last_telemetry TEXT,
                    created_at TEXT DEFAULT CURRENT_TIMESTAMP
                )
            """)
            await db.execute("""
                CREATE TABLE IF NOT EXISTS telemetry (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    device_id TEXT,
                    sensor TEXT,
                    value REAL,
                    value_bool INTEGER,
                    timestamp TEXT,
                    FOREIGN KEY (device_id) REFERENCES devices(device_id)
                )
            """)
            await db.execute("""
                CREATE INDEX IF NOT EXISTS idx_telemetry_lookup
                ON telemetry(device_id, sensor, timestamp)
            """)
            await db.commit()
        self._initialized = True

    async def update_device_status(self, device_id: str, status: str):
        async with aiosqlite.connect(DB_PATH) as db:
            await db.execute("""
                INSERT INTO devices (device_id, status, last_seen)
                VALUES (?, ?, ?)
                ON CONFLICT(device_id) DO UPDATE SET
                    status = excluded.status,
                    last_seen = excluded.last_seen
            """, (device_id, status, datetime.now().isoformat()))
            await db.commit()

    async def update_device_telemetry(self, device_id: str, payload: dict):
        async with aiosqlite.connect(DB_PATH) as db:
            room = payload.get("room")
            now = datetime.now().isoformat()
            await db.execute("""
                INSERT INTO devices (device_id, room, status, last_seen, last_telemetry)
                VALUES (?, ?, 'online', ?, ?)
                ON CONFLICT(device_id) DO UPDATE SET
                    room = COALESCE(excluded.room, room),
                    status = 'online',
                    last_seen = excluded.last_seen,
                    last_telemetry = excluded.last_telemetry
            """, (device_id, room, now, json.dumps(payload)))

            numeric_sensors = ["temperature", "humidity", "light", "pressure", "co2", "battery"]
            bool_sensors = ["presence", "relay"]

            for sensor in numeric_sensors:
                if sensor in payload and payload[sensor] is not None:
                    await db.execute("""
                        INSERT INTO telemetry (device_id, sensor, value, timestamp)
                        VALUES (?, ?, ?, ?)
                    """, (device_id, sensor, float(payload[sensor]), now))

            for sensor in bool_sensors:
                if sensor in payload and payload[sensor] is not None:
                    await db.execute("""
                        INSERT INTO telemetry (device_id, sensor, value_bool, timestamp)
                        VALUES (?, ?, ?, ?)
                    """, (device_id, sensor, int(bool(payload[sensor])), now))

            await db.commit()

    async def get_devices(self) -> List[Dict]:
        async with aiosqlite.connect(DB_PATH) as db:
            db.row_factory = aiosqlite.Row
            async with db.execute("SELECT * FROM devices ORDER BY last_seen DESC") as cursor:
                rows = await cursor.fetchall()
                return [dict(row) for row in rows]

    async def get_history(self, device_id: str, sensor: str, hours: int = 24) -> List[Dict]:
        since = (datetime.now() - timedelta(hours=hours)).isoformat()
        async with aiosqlite.connect(DB_PATH) as db:
            db.row_factory = aiosqlite.Row
            async with db.execute("""
                SELECT timestamp, COALESCE(value, value_bool) as value
                FROM telemetry
                WHERE device_id = ? AND sensor = ? AND timestamp > ?
                ORDER BY timestamp ASC
                LIMIT 500
            """, (device_id, sensor, since)) as cursor:
                rows = await cursor.fetchall()
                return [dict(row) for row in rows]

    async def delete_old_records(self, days: int = 30):
        cutoff = (datetime.now() - timedelta(days=days)).isoformat()
        async with aiosqlite.connect(DB_PATH) as db:
            await db.execute("DELETE FROM telemetry WHERE timestamp < ?", (cutoff,))
            await db.commit()
