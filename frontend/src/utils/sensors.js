export const SENSOR_CONFIG = {
  temperature: {
    label: 'Température',
    unit: '°C',
    color: '#f97316',
    min: -10,
    max: 50,
    type: 'numeric',
    icon: 'thermometer',
  },
  humidity: {
    label: 'Humidité',
    unit: '%',
    color: '#3b82f6',
    min: 0,
    max: 100,
    type: 'numeric',
    icon: 'droplets',
  },
  light: {
    label: 'Luminosité',
    unit: 'lux',
    color: '#eab308',
    min: 0,
    max: 10000,
    type: 'numeric',
    icon: 'sun',
  },
  pressure: {
    label: 'Pression',
    unit: 'hPa',
    color: '#8b5cf6',
    min: 900,
    max: 1100,
    type: 'numeric',
    icon: 'gauge',
  },
  co2: {
    label: 'CO₂',
    unit: 'ppm',
    color: '#06b6d4',
    min: 400,
    max: 2000,
    type: 'numeric',
    icon: 'wind',
  },
  battery: {
    label: 'Batterie',
    unit: '%',
    color: '#22c55e',
    min: 0,
    max: 100,
    type: 'numeric',
    icon: 'battery',
  },
  presence: {
    label: 'Présence',
    unit: '',
    color: '#ec4899',
    type: 'boolean',
    icon: 'user',
  },
  relay: {
    label: 'Relais',
    unit: '',
    color: '#f59e0b',
    type: 'boolean',
    icon: 'zap',
  },
}

export const NUMERIC_SENSORS = Object.entries(SENSOR_CONFIG)
  .filter(([, cfg]) => cfg.type === 'numeric')
  .map(([key]) => key)

export const BOOL_SENSORS = Object.entries(SENSOR_CONFIG)
  .filter(([, cfg]) => cfg.type === 'boolean')
  .map(([key]) => key)

export function getSensorKeys(sensors) {
  return Object.keys(sensors || {}).filter((k) => SENSOR_CONFIG[k] && k !== 'room')
}

export function formatValue(key, value) {
  const cfg = SENSOR_CONFIG[key]
  if (!cfg) return String(value)
  if (cfg.type === 'boolean') return value ? 'Oui' : 'Non'
  if (typeof value === 'number') return `${value.toFixed(1)}${cfg.unit}`
  return `${value}${cfg.unit}`
}
