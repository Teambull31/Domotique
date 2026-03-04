import { useState, useEffect, useCallback, useRef } from 'react'
import Navbar from './components/Navbar'
import DeviceCard from './components/DeviceCard'
import DeviceDetail from './components/DeviceDetail'

const WS_URL = import.meta.env.VITE_WS_URL
  || `${window.location.protocol === 'https:' ? 'wss' : 'ws'}://${window.location.host}/ws`

export default function App() {
  const [devices, setDevices] = useState({})
  const [selectedId, setSelectedId] = useState(null)
  const [connected, setConnected] = useState(false)
  const wsRef = useRef(null)
  const reconnectTimer = useRef(null)

  const connectWebSocket = useCallback(() => {
    if (wsRef.current?.readyState === WebSocket.OPEN) return

    const ws = new WebSocket(WS_URL)
    wsRef.current = ws

    ws.onopen = () => {
      setConnected(true)
      if (reconnectTimer.current) {
        clearTimeout(reconnectTimer.current)
        reconnectTimer.current = null
      }
    }

    ws.onmessage = (event) => {
      const msg = JSON.parse(event.data)

      if (msg.type === 'init') {
        const map = {}
        msg.devices.forEach((d) => {
          map[d.device_id] = {
            ...d,
            sensors: d.last_telemetry ? JSON.parse(d.last_telemetry) : {},
          }
        })
        setDevices(map)
      } else if (msg.type === 'telemetry') {
        setDevices((prev) => ({
          ...prev,
          [msg.device_id]: {
            ...prev[msg.device_id],
            device_id: msg.device_id,
            status: 'online',
            last_seen: msg.timestamp,
            sensors: {
              ...(prev[msg.device_id]?.sensors || {}),
              ...msg.data,
            },
          },
        }))
      } else if (msg.type === 'status') {
        setDevices((prev) => ({
          ...prev,
          [msg.device_id]: {
            ...prev[msg.device_id],
            device_id: msg.device_id,
            status: msg.status,
            last_seen: msg.timestamp,
          },
        }))
      }
    }

    ws.onclose = () => {
      setConnected(false)
      reconnectTimer.current = setTimeout(connectWebSocket, 3000)
    }

    ws.onerror = () => ws.close()
  }, [])

  useEffect(() => {
    connectWebSocket()
    // Send ping every 30s to keep connection alive
    const ping = setInterval(() => {
      if (wsRef.current?.readyState === WebSocket.OPEN) {
        wsRef.current.send('ping')
      }
    }, 30_000)

    return () => {
      clearInterval(ping)
      clearTimeout(reconnectTimer.current)
      wsRef.current?.close()
    }
  }, [connectWebSocket])

  const deviceList = Object.values(devices).sort((a, b) => {
    if (a.status === 'online' && b.status !== 'online') return -1
    if (a.status !== 'online' && b.status === 'online') return 1
    return (b.last_seen || '').localeCompare(a.last_seen || '')
  })

  const onlineCount = deviceList.filter((d) => d.status === 'online').length
  const selectedDevice = selectedId ? devices[selectedId] : null

  return (
    <div className="min-h-screen bg-slate-900">
      <Navbar connected={connected} deviceCount={deviceList.length} onlineCount={onlineCount} />

      <main className="container mx-auto px-4 py-8">
        {deviceList.length === 0 ? (
          <EmptyState />
        ) : (
          <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 xl:grid-cols-4 gap-5">
            {deviceList.map((device) => (
              <DeviceCard
                key={device.device_id}
                device={device}
                onClick={() => setSelectedId(device.device_id)}
              />
            ))}
          </div>
        )}
      </main>

      {selectedDevice && (
        <DeviceDetail
          device={selectedDevice}
          onClose={() => setSelectedId(null)}
        />
      )}
    </div>
  )
}

function EmptyState() {
  return (
    <div className="flex flex-col items-center justify-center min-h-[60vh] text-center px-4">
      <div className="w-20 h-20 rounded-full bg-slate-800 border border-slate-700 flex items-center justify-center text-4xl mb-6">
        📡
      </div>
      <h2 className="text-xl font-bold text-slate-100 mb-2">En attente des dispositifs</h2>
      <p className="text-slate-400 max-w-sm mb-8">
        Les ESP32 apparaîtront ici automatiquement dès qu'ils enverront des données MQTT.
      </p>

      <div className="bg-slate-800 border border-slate-700 rounded-xl p-5 text-left max-w-lg w-full">
        <p className="text-slate-400 text-xs font-semibold uppercase tracking-wider mb-3">
          Format des messages MQTT attendu
        </p>
        <div className="space-y-3 text-sm">
          <div>
            <p className="text-slate-500 text-xs mb-1">Topic de télémétrie :</p>
            <code className="block bg-slate-900 text-blue-300 px-3 py-2 rounded-lg text-xs">
              home/devices/{'<device_id>'}/telemetry
            </code>
          </div>
          <div>
            <p className="text-slate-500 text-xs mb-1">Payload JSON :</p>
            <pre className="bg-slate-900 text-green-300 px-3 py-2 rounded-lg text-xs overflow-x-auto">
{`{
  "room": "salon",
  "temperature": 23.5,
  "humidity": 65.2,
  "light": 450,
  "presence": false
}`}
            </pre>
          </div>
          <div>
            <p className="text-slate-500 text-xs mb-1">Status (Last Will Testament) :</p>
            <code className="block bg-slate-900 text-yellow-300 px-3 py-2 rounded-lg text-xs">
              home/devices/{'<device_id>'}/status → "online" | "offline"
            </code>
          </div>
        </div>
      </div>
    </div>
  )
}
