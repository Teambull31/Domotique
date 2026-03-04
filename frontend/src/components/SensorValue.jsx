import {
  Thermometer, Droplets, Sun, Gauge, Wind, Battery, User, Zap,
} from 'lucide-react'
import { SENSOR_CONFIG } from '../utils/sensors'

const ICONS = {
  thermometer: Thermometer,
  droplets: Droplets,
  sun: Sun,
  gauge: Gauge,
  wind: Wind,
  battery: Battery,
  user: User,
  zap: Zap,
}

export default function SensorValue({ sensorKey, value, compact = false }) {
  const cfg = SENSOR_CONFIG[sensorKey]
  if (!cfg) return null

  const Icon = ICONS[cfg.icon] || Gauge
  const isBool = cfg.type === 'boolean'

  const displayValue = isBool
    ? (value ? 'Détecté' : 'Absent')
    : typeof value === 'number'
      ? value.toFixed(1)
      : String(value)

  if (compact) {
    return (
      <div
        className="flex flex-col gap-0.5 bg-slate-900/50 rounded-xl p-3 border border-slate-700/50"
        style={{ borderLeftColor: cfg.color, borderLeftWidth: 2 }}
      >
        <div className="flex items-center gap-1 text-slate-400 text-xs">
          <Icon size={11} style={{ color: cfg.color }} />
          <span>{cfg.label}</span>
        </div>
        <div className="font-semibold text-slate-100 text-sm">
          {displayValue}
          {!isBool && <span className="text-slate-400 text-xs ml-0.5">{cfg.unit}</span>}
        </div>
      </div>
    )
  }

  return (
    <div className="flex items-center justify-between p-4 bg-slate-900/50 rounded-xl border border-slate-700/50">
      <div className="flex items-center gap-3">
        <div
          className="w-9 h-9 rounded-lg flex items-center justify-center"
          style={{ backgroundColor: `${cfg.color}20` }}
        >
          <Icon size={18} style={{ color: cfg.color }} />
        </div>
        <div>
          <p className="text-slate-400 text-xs">{cfg.label}</p>
          <p className="font-semibold text-slate-100">
            {displayValue}
            {!isBool && <span className="text-slate-400 text-sm ml-0.5">{cfg.unit}</span>}
          </p>
        </div>
      </div>

      {/* Mini bar for numeric sensors */}
      {!isBool && typeof value === 'number' && cfg.min !== undefined && (
        <div className="w-16 h-1.5 bg-slate-700 rounded-full overflow-hidden">
          <div
            className="h-full rounded-full transition-all duration-500"
            style={{
              width: `${Math.min(100, Math.max(0, ((value - cfg.min) / (cfg.max - cfg.min)) * 100))}%`,
              backgroundColor: cfg.color,
            }}
          />
        </div>
      )}

      {/* Toggle indicator for booleans */}
      {isBool && (
        <div
          className={`w-8 h-4 rounded-full transition-colors duration-300 flex items-center ${
            value ? 'bg-green-500 justify-end' : 'bg-slate-600 justify-start'
          }`}
        >
          <div className="w-3 h-3 bg-white rounded-full mx-0.5 shadow" />
        </div>
      )}
    </div>
  )
}
