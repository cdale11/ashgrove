import type { WorldState } from '../types'

interface TimePanelProps {
  state: WorldState
}

export function TimePanel({ state }: TimePanelProps) {
  const t = state.time_data
  const hour = String(t.hour).padStart(2, '0')
  const minute = String(t.minute).padStart(2, '0')
  const month = Math.floor((t.day_of_year - 1) / 30) + 1
  const day = ((t.day_of_year - 1) % 30) + 1

  return (
    <div className="panel time-panel">
      <h3>Ashgrove</h3>
      <div className="time-display">
        <span className="clock">{hour}:{minute}</span>
        <span className="date">Day {day}, Month {month}, Year {t.year}</span>
      </div>
      <div className="condition-row">
        <span className={`season season-${t.season.toLowerCase()}`}>{t.season}</span>
        <span className="weather">
          {t.weather}
          {t.weather_intensity >= 0.7 ? ' (heavy)' : t.weather_intensity >= 0.4 ? ' (moderate)' : ' (light)'}
        </span>
      </div>
      <div className="meta-row">
        <span>Tick {t.ticks}</span>
      </div>
    </div>
  )
}