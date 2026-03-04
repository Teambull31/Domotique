import { useEffect, useState } from 'react'
import {
  X, Wifi, WifiOff, MapPin, Send, ToggleLeft, ToggleRight, Monitor,
} from 'lucide-react'
import {
  AreaChart, Area, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer,
} from 'recharts'
import SensorValue from './SensorValue'
import { getSensorKeys, SENSOR_CONFIG, NUMERIC_SENSORS } from '../utils/sensors'
import { formatTimeAgo, formatTimestamp } from '../utils/time'

const API_URL = import.meta.env.VITE_API_URL || '/api'

function HistoryChart({ deviceId, sensorKey }) {
  const [data, setData] = useState([])
  const [loading, setLoading] = useState(true)
  const cfg = SENSOR_CONFIG[sensorKey]

  useEffect(() => {
    setLoading(true)
    fetch(`${API_URL}/devices/${deviceId}/history/${sensorKey}?hours=24`)
      .then((r) => r.json())
      .then((rows) => {
        setData(
          rows.map((row) => ({
            time: formatTimestamp(row.timestamp),
            value: Math.round(row.value * 10) / 10,
          })),
        )
      })
      .catch(console.error)
      .finally(() => setLoading(false))
  }, [deviceId, sensorKey])

  if (loading) {
    return (
      <div className="h-40 flex items-center justify-center text-slate-500 text-sm">
        Chargement...
      </div>
    )
  }

  if (data.length === 0) {
    return (
      <div className="h-40 flex items-center justify-center text-slate-500 text-sm">
        Pas encore de données
      </div>
    )
  }

  return (
    <ResponsiveContainer width="100%" height={160}>
      <AreaChart data={data} margin={{ top: 5, right: 5, left: -20, bottom: 0 }}>
        <defs>
          <linearGradient id={`grad-${sensorKey}`} x1="0" y1="0" x2="0" y2="1">
            <stop offset="5%" stopColor={cfg.color} stopOpacity={0.3} />
            <stop offset="95%" stopColor={cfg.color} stopOpacity={0} />
          </linearGradient>
        </defs>
        <CartesianGrid strokeDasharray="3 3" stroke="#334155" />
        <XAxis
          dataKey="time"
          tick={{ fontSize: 10, fill: '#64748b' }}
          tickLine={false}
          axisLine={false}
          interval="preserveStartEnd"
        />
        <YAxis
          tick={{ fontSize: 10, fill: '#64748b' }}
          tickLine={false}
          axisLine={false}
          domain={['auto', 'auto']}
        />
        <Tooltip
          contentStyle={{
            backgroundColor: '#1e293b',
            border: '1px solid #334155',
            borderRadius: '8px',
            fontSize: '12px',
            color: '#f1f5f9',
          }}
          formatter={(v) => [`${v}${cfg.unit}`, cfg.label]}
        />
        <Area
          type="monotone"
          dataKey="value"
          stroke={cfg.color}
          strokeWidth={2}
          fill={`url(#grad-${sensorKey})`}
          dot={false}
          activeDot={{ r: 4, fill: cfg.color }}
        />
      </AreaChart>
    </ResponsiveContainer>
  )
}

function CommandPanel({ deviceId }) {
  const [relayState, setRelayState] = useState(false)
  const [displayText, setDisplayText] = useState('')
  const [sending, setSending] = useState(false)
  const [result, setResult] = useState(null)

  const sendCommand = async (payload) => {
    setSending(true)
    setResult(null)
    try {
      const res = await fetch(`${API_URL}/devices/${deviceId}/command`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload),
      })
      const data = await res.json()
      setResult(res.ok ? 'Commande envoyée ✓' : `Erreur: ${data.message}`)
    } catch {
      setResult('Erreur de connexion')
    } finally {
      setSending(false)
      setTimeout(() => setResult(null), 3000)
    }
  }

  return (
    <div className="space-y-4">
      {/* Relay */}
      <div className="flex items-center justify-between p-4 bg-slate-900/50 rounded-xl border border-slate-700/50">
        <div className="flex items-center gap-3">
          <div className="w-8 h-8 rounded-lg bg-amber-500/20 flex items-center justify-center">
            {relayState ? (
              <ToggleRight size={16} className="text-amber-400" />
            ) : (
              <ToggleLeft size={16} className="text-slate-400" />
            )}
          </div>
          <span className="text-sm font-medium">Relais</span>
        </div>
        <button
          onClick={() => {
            const next = !relayState
            setRelayState(next)
            sendCommand({ relay: next })
          }}
          disabled={sending}
          className={`relative w-12 h-6 rounded-full transition-colors duration-300 ${
            relayState ? 'bg-amber-500' : 'bg-slate-600'
          }`}
        >
          <div
            className={`absolute top-1 w-4 h-4 bg-white rounded-full shadow transition-all duration-300 ${
              relayState ? 'left-7' : 'left-1'
            }`}
          />
        </button>
      </div>

      {/* Display text */}
      <div className="p-4 bg-slate-900/50 rounded-xl border border-slate-700/50 space-y-3">
        <div className="flex items-center gap-2 text-sm font-medium">
          <Monitor size={14} className="text-blue-400" />
          Afficher sur l'écran
        </div>
        <div className="flex gap-2">
          <input
            type="text"
            value={displayText}
            onChange={(e) => setDisplayText(e.target.value)}
            onKeyDown={(e) => e.key === 'Enter' && displayText && sendCommand({ display: displayText })}
            placeholder="Texte à afficher..."
            maxLength={64}
            className="flex-1 bg-slate-800 border border-slate-600 rounded-lg px-3 py-2 text-sm
                       text-slate-100 placeholder-slate-500 focus:outline-none focus:border-blue-500
                       focus:ring-1 focus:ring-blue-500"
          />
          <button
            onClick={() => displayText && sendCommand({ display: displayText })}
            disabled={!displayText || sending}
            className="px-3 py-2 bg-blue-600 hover:bg-blue-500 disabled:bg-slate-700
                       disabled:text-slate-500 text-white rounded-lg transition-colors"
          >
            <Send size={14} />
          </button>
        </div>
      </div>

      {result && (
        <p className="text-center text-sm text-green-400 animate-pulse">{result}</p>
      )}
    </div>
  )
}

export default function DeviceDetail({ device, onClose }) {
  const isOnline = device.status === 'online'
  const sensors = device.sensors || {}
  const sensorKeys = getSensorKeys(sensors)
  const numericSensorKeys = sensorKeys.filter((k) => NUMERIC_SENSORS.includes(k))

  // Close on Escape key
  useEffect(() => {
    const handler = (e) => e.key === 'Escape' && onClose()
    window.addEventListener('keydown', handler)
    return () => window.removeEventListener('keydown', handler)
  }, [onClose])

  return (
    <div
      className="fixed inset-0 z-50 flex items-end sm:items-center justify-center p-0 sm:p-4"
      onClick={(e) => e.target === e.currentTarget && onClose()}
    >
      {/* Backdrop */}
      <div className="absolute inset-0 bg-slate-900/80 backdrop-blur-sm" onClick={onClose} />

      {/* Panel */}
      <div className="relative w-full sm:max-w-2xl max-h-[90vh] overflow-y-auto bg-slate-800
                      border border-slate-700 rounded-t-3xl sm:rounded-2xl shadow-2xl">
        {/* Header */}
        <div className="sticky top-0 bg-slate-800 border-b border-slate-700/50 p-5 flex items-start justify-between z-10">
          <div>
            <div className="flex items-center gap-2 mb-1">
              <span
                className={`w-2.5 h-2.5 rounded-full ${
                  isOnline ? 'bg-green-400 animate-pulse-slow' : 'bg-slate-500'
                }`}
              />
              <h2 className="text-lg font-bold">{device.device_id}</h2>
            </div>
            <div className="flex items-center gap-3 text-sm text-slate-400 ml-4">
              {device.room && (
                <span className="flex items-center gap-1">
                  <MapPin size={12} /> {device.room}
                </span>
              )}
              <span
                className={`flex items-center gap-1 ${isOnline ? 'text-green-400' : 'text-slate-500'}`}
              >
                {isOnline ? <Wifi size={12} /> : <WifiOff size={12} />}
                {isOnline ? 'En ligne' : 'Hors ligne'}
              </span>
              <span className="text-slate-500">{formatTimeAgo(device.last_seen)}</span>
            </div>
          </div>
          <button
            onClick={onClose}
            className="p-2 hover:bg-slate-700 rounded-lg transition-colors text-slate-400 hover:text-slate-100"
          >
            <X size={18} />
          </button>
        </div>

        <div className="p-5 space-y-6">
          {/* Current sensors */}
          {sensorKeys.length > 0 && (
            <section>
              <h3 className="text-xs font-semibold text-slate-400 uppercase tracking-wider mb-3">
                Capteurs actuels
              </h3>
              <div className="grid grid-cols-1 sm:grid-cols-2 gap-3">
                {sensorKeys.map((key) => (
                  <SensorValue key={key} sensorKey={key} value={sensors[key]} />
                ))}
              </div>
            </section>
          )}

          {/* History charts */}
          {numericSensorKeys.length > 0 && (
            <section>
              <h3 className="text-xs font-semibold text-slate-400 uppercase tracking-wider mb-3">
                Historique (24h)
              </h3>
              <div className="space-y-4">
                {numericSensorKeys.map((key) => (
                  <div key={key} className="bg-slate-900/50 rounded-xl p-4 border border-slate-700/50">
                    <p className="text-xs font-medium text-slate-400 mb-3 flex items-center gap-1">
                      <span
                        className="w-2 h-2 rounded-full"
                        style={{ backgroundColor: SENSOR_CONFIG[key].color }}
                      />
                      {SENSOR_CONFIG[key].label}
                    </p>
                    <HistoryChart deviceId={device.device_id} sensorKey={key} />
                  </div>
                ))}
              </div>
            </section>
          )}

          {/* Command panel */}
          <section>
            <h3 className="text-xs font-semibold text-slate-400 uppercase tracking-wider mb-3">
              Commandes
            </h3>
            <CommandPanel deviceId={device.device_id} />
          </section>
        </div>
      </div>
    </div>
  )
}
