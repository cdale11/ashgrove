import { useState, useEffect } from 'react'

interface LensTarget {
  type: 'building' | 'npc' | 'item' | 'tile'
  id: number
  name: string
  description: string
  sensory?: string[]
  knowledge?: string[]
}

interface LensPanelProps {
  target: LensTarget | null
  onClose: () => void
}

const SENSORY_KEYS = ['scent', 'sound', 'touch', 'temperature', 'air'] as const

export function LensPanel({ target, onClose }: LensPanelProps) {
  const [visible, setVisible] = useState(false)

  useEffect(() => {
    if (target) setVisible(true)
  }, [target])

  if (!visible || !target) return null

  const sensory = target.sensory ?? []

  return (
    <div className="lens-overlay" onClick={onClose}>
      <div className="lens-panel" onClick={(e) => e.stopPropagation()}>
        <div className="lens-header">
          <h3>{target.name}</h3>
          <button className="lens-close" onClick={onClose} aria-label="Close">✕</button>
        </div>

        <div className="lens-body">
          <p className="lens-description">{target.description}</p>

          {sensory.length > 0 && (
            <div className="lens-sensory">
              <span className="sensory-label">Senses:</span>
              <ul>
                {sensory.map((s, i) => (
                  <li key={i}>{s}</li>
                ))}
              </ul>
            </div>
          )}

          {target.knowledge && target.knowledge.length > 0 && (
            <div className="lens-knowledge">
              <span className="sensory-label">Gleaned:</span>
              <ul>
                {target.knowledge.map((k, i) => (
                  <li key={i}>{k}</li>
                ))}
              </ul>
            </div>
          )}

          <div className="lens-actions">
            <button className="lens-back" onClick={onClose}>Look away</button>
          </div>
        </div>
      </div>
    </div>
  )
}