import { useState } from 'react'
import type { WorldState } from '../types'

interface TimePanelProps {
  state: WorldState
  onAdvanceTime: (hours: number) => Promise<void>
}

export function TimePanel({ state, onAdvanceTime }: TimePanelProps) {
  const t = state.time_data
  const hour = String(t.hour).padStart(2, '0')
  const minute = String(t.minute).padStart(2, '0')
  const month = Math.floor((t.day_of_year - 1) / 30) + 1
  const day = ((t.day_of_year - 1) % 30) + 1
  const [skipping, setSkipping] = useState(false)

  const handleSkip = async (hours: number) => {
    setSkipping(true)
    try {
      await onAdvanceTime(hours)
    } finally {
      setSkipping(false)
    }
  }

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

      <div className="time-skip">
        <button className="skip-btn" onClick={() => handleSkip(1)} disabled={skipping}>+1h</button>
        <button className="skip-btn" onClick={() => handleSkip(6)} disabled={skipping}>+6h</button>
        <button className="skip-btn" onClick={() => handleSkip(12)} disabled={skipping}>+12h</button>
        <button className="skip-btn" onClick={() => handleSkip(24)} disabled={skipping}>+1d</button>
        <button className="skip-btn dawn" onClick={() => handleSkip(t.hour < 7 ? 7 - t.hour : 24 + 7 - t.hour)} disabled={skipping}>
          Next Dawn
        </button>
        <button className="skip-btn dusk" onClick={() => handleSkip(t.hour < 19 ? 19 - t.hour : 24 + 19 - t.hour)} disabled={skipping}>
          Next Dusk
        </button>
      </div>
    </div>
  )
}