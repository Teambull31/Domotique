import { Wifi, WifiOff, Home, Activity } from 'lucide-react'

export default function Navbar({ connected, deviceCount, onlineCount }) {
  return (
    <header className="sticky top-0 z-50 glass border-b border-slate-700/50">
      <div className="container mx-auto px-4 h-16 flex items-center justify-between">
        {/* Logo */}
        <div className="flex items-center gap-3">
          <div className="w-8 h-8 bg-blue-500 rounded-lg flex items-center justify-center">
            <Home size={16} className="text-white" />
          </div>
          <span className="font-bold text-lg tracking-tight">Domotique</span>
        </div>

        {/* Stats */}
        <div className="hidden sm:flex items-center gap-6 text-sm text-slate-400">
          <div className="flex items-center gap-1.5">
            <Activity size={14} />
            <span>
              <span className="text-slate-100 font-medium">{onlineCount}</span>
              {' / '}
              <span>{deviceCount}</span>
              {' en ligne'}
            </span>
          </div>
        </div>

        {/* Connection status */}
        <div
          className={`flex items-center gap-2 px-3 py-1.5 rounded-full text-xs font-medium border transition-colors ${
            connected
              ? 'bg-green-500/10 text-green-400 border-green-500/30'
              : 'bg-red-500/10 text-red-400 border-red-500/30'
          }`}
        >
          <span
            className={`w-1.5 h-1.5 rounded-full ${
              connected ? 'bg-green-400 animate-pulse' : 'bg-red-400'
            }`}
          />
          {connected ? (
            <><Wifi size={12} /> Connecté</>
          ) : (
            <><WifiOff size={12} /> Reconnexion...</>
          )}
        </div>
      </div>
    </header>
  )
}
