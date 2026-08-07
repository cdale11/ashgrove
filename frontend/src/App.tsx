import { useState } from 'react'
import { useGameState } from './hooks/useGameState'
import { TimePanel } from './components/TimePanel'
import { VillagePanel } from './components/VillagePanel'
import { NPCDetailPanel } from './components/NPCDetailPanel'
import { InvestigationPanel } from './components/InvestigationPanel'
import { WorldPanel } from './components/WorldPanel'
import './App.css'

export default function App() {
  const { state, connected, error, saveGame, loadGame } = useGameState()
  const [selectedNpcId, setSelectedNpcId] = useState<number | null>(null)
  const [notifications, setNotifications] = useState<string[]>([])

  const pushNotice = (msg: string) => {
    setNotifications((n) => [msg, ...n].slice(0, 5))
    setTimeout(() => setNotifications((n) => n.filter((x) => x !== msg)), 4000)
  }

  const selectedNpc = state?.world.npcs.find((n) => n.id === selectedNpcId) ?? null

  const handleSave = () => {
    saveGame()
    pushNotice('Save requested')
  }
  const handleLoad = () => {
    loadGame()
    pushNotice('Load requested')
  }

  if (!state) {
    return (
      <div className="app loading-screen">
        <div className="loading-card">
          <h1>Ashgrove</h1>
          <p className="loading-status">{error ?? 'Connecting to the world...'}</p>
          {!connected && <p className="hint">Ensure the game server is running on port 8000</p>}
        </div>
      </div>
    )
  }

  return (
    <div className="app">
      <header className="topbar">
        <div className="brand">
          <span className="brand-name">Ashgrove</span>
          <span className={`conn ${connected ? 'conn-ok' : 'conn-warn'}`}>
            {connected ? '● live' : '○ connecting'}
          </span>
        </div>
        <div className="actions">
          <button onClick={handleSave} disabled={!connected}>Save</button>
          <button onClick={handleLoad} disabled={!connected}>Load</button>
        </div>
      </header>

      <div className="notification-area">
        {notifications.map((n, i) => (
          <div key={i} className="notification">{n}</div>
        ))}
      </div>

      <main className="layout">
        <aside className="column left">
          <TimePanel state={state} />
          <VillagePanel
            state={state}
            selectedNpcId={selectedNpcId}
            onSelectNpc={setSelectedNpcId}
          />
        </aside>

        <section className="column center">
          {selectedNpc ? (
            <NPCDetailPanel state={state} npc={selectedNpc} />
          ) : (
            <div className="panel placeholder">
              <h3>Select a villager</h3>
              <p>
                Click on a villager in the left panel to inspect their personality,
                beliefs, goals, relationships, and memories.
              </p>
              <p className="smoke">
                The village is alive. Choose who to get close to — and who to beware of.
              </p>
            </div>
          )}
        </section>

        <aside className="column right">
          <InvestigationPanel state={state} />
          <WorldPanel state={state} />
        </aside>
      </main>

      <footer className="footer">
        <span>Ashgrove v0.1.0 · C++ simulation server · Web client</span>
        {error && <span className="error-text">{error}</span>}
      </footer>
    </div>
  )
}