import { Wifi, WifiOff, ChevronRight, MapPin } from 'lucide-react'
import SensorValue from './SensorValue'
import { getSensorKeys } from '../utils/sensors'
import { formatTimeAgo } from '../utils/time'

export default function DeviceCard({ device, onClick }) {
  const isOnline = device.status === 'online'
  const sensors = device.sensors || {}
  const sensorKeys = getSensorKeys(sensors)

  return (
    <button
      onClick={onClick}
      className="w-full text-left bg-slate-800 border border-slate-700 rounded-2xl p-5
                 hover:border-blue-500/40 hover:bg-slate-800/80 transition-all duration-200
                 hover:shadow-xl hover:shadow-blue-500/5 focus:outline-none focus:ring-2
                 focus:ring-blue-500/50 group"
    >
      {/* Header */}
      <div className="flex items-start justify-between mb-4">
        <div className="min-w-0 flex-1">
          <div className="flex items-center gap-2 mb-1">
            {/* Online dot */}
            <span
              className={`w-2 h-2 rounded-full flex-shrink-0 ${
                isOnline ? 'bg-green-400 animate-pulse-slow' : 'bg-slate-500'
              }`}
            />
            <h3 className="font-semibold text-slate-100 truncate">{device.device_id}</h3>
          </div>
          {device.room && (
            <div className="flex items-center gap-1 text-slate-400 text-xs ml-4">
              <MapPin size={10} />
              <span>{device.room}</span>
            </div>
          )}
        </div>

        <div
          className={`flex items-center gap-1 px-2 py-0.5 rounded-full text-xs font-medium border flex-shrink-0 ml-2 ${
            isOnline
              ? 'bg-green-500/10 text-green-400 border-green-500/30'
              : 'bg-slate-700/50 text-slate-500 border-slate-600/30'
          }`}
        >
          {isOnline ? <Wifi size={10} /> : <WifiOff size={10} />}
          {isOnline ? 'En ligne' : 'Hors ligne'}
        </div>
      </div>

      {/* Sensors */}
      {sensorKeys.length > 0 ? (
        <div className="grid grid-cols-2 gap-2">
          {sensorKeys.slice(0, 4).map((key) => (
            <SensorValue key={key} sensorKey={key} value={sensors[key]} compact />
          ))}
          {sensorKeys.length > 4 && (
            <div className="col-span-2 text-center text-slate-500 text-xs py-1">
              +{sensorKeys.length - 4} capteur{sensorKeys.length - 4 > 1 ? 's' : ''}
            </div>
          )}
        </div>
      ) : (
        <p className="text-slate-500 text-sm italic text-center py-4">Aucun capteur détecté</p>
      )}

      {/* Footer */}
      <div className="mt-4 pt-3 border-t border-slate-700/50 flex items-center justify-between">
        <span className="text-slate-500 text-xs">{formatTimeAgo(device.last_seen)}</span>
        <span className="text-blue-400 text-xs flex items-center gap-1 group-hover:translate-x-0.5 transition-transform">
          Détails <ChevronRight size={12} />
        </span>
      </div>
    </button>
  )
}
