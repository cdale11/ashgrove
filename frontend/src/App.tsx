import { useState } from 'react'
import { useGameState } from './hooks/useGameState'
import { TimePanel } from './components/TimePanel'
import { VillagePanel } from './components/VillagePanel'
import { NPCDetailPanel } from './components/NPCDetailPanel'
import { InvestigationPanel } from './components/InvestigationPanel'
import { WorldPanel } from './components/WorldPanel'
import { MapPanel } from './components/MapPanel'
import { PlayerPanel } from './components/PlayerPanel'
import { DialoguePanel } from './components/DialoguePanel'
import { InteriorPanel } from './components/InteriorPanel'
import { LensPanel } from './components/LensPanel'
import { JournalPanel } from './components/JournalPanel'
import { InventoryPanel } from './components/InventoryPanel'
import { LifestylePanel } from './components/LifestylePanel'
import { Accordion } from './components/Accordion'
import type { DialogueSession } from './components/DialoguePanel'
import type { ConversationTopic, DialogueLine } from './types'
import './App.css'

export default function App() {
  const { state, connected, error, saveGame, loadGame, act } = useGameState()
  const [selectedNpcId, setSelectedNpcId] = useState<number | null>(null)
  const [notifications, setNotifications] = useState<string[]>([])
  const [dialogue, setDialogue] = useState<DialogueSession | null>(null)
  const [focusedTarget, setFocusedTarget] = useState<{
    type: 'building' | 'npc' | 'item'
    id: number
    name: string
    description: string
    sensory?: string[]
    knowledge?: string[]
  } | null>(null)
  const [showJournal, setShowJournal] = useState(false)
  const [showInventory, setShowInventory] = useState(false)
  const [showLifestyle, setShowLifestyle] = useState(false)

  const pushNotice = (msg: string) => {
    if (!msg) return
    setNotifications((n) => [msg, ...n].slice(0, 5))
    setTimeout(() => setNotifications((n) => n.filter((x) => x !== msg)), 5000)
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

  const handleTalk = async (npcId: number) => {
    const npc = state?.world.npcs.find((n) => n.id === npcId)
    const name = npc ? `${npc.name} ${npc.surname}`.trim() : 'Villager'
    // Open the panel immediately so the player gets feedback while the LLM
    // generates a reply (single-core llama.cpp can take tens of seconds).
    setDialogue({
      npcId,
      npcName: name,
      entries: [{ speaker: name, text: '…' }],
      topics: [],
    })
    const res = await act({ type: 'talk', target: npcId })
    if (!res.ok) {
      setDialogue((d) =>
        d && d.npcId === npcId
          ? { ...d, entries: [...d.entries.slice(0, -1), { speaker: name, text: String(res.error ?? 'Cannot talk to that person') }] }
          : d,
      )
      return
    }
    const line = res.line as DialogueLine | undefined
    const topics = (res.topics ?? []) as ConversationTopic[]
    setDialogue((d) =>
      d && d.npcId === npcId
        ? {
            ...d,
            npcName: String(res.speaker_name ?? name),
            entries: [...d.entries.slice(0, -1), { speaker: String(res.speaker_name ?? name), text: line?.text ?? '' }],
            topics,
          }
        : {
            npcId,
            npcName: String(res.speaker_name ?? name),
            entries: [{ speaker: String(res.speaker_name ?? name), text: line?.text ?? '' }],
            topics,
          },
    )
  }

  const handleInspect = async (npcId: number) => {
    setSelectedNpcId(npcId)
    const res = await act({ type: 'inspect', what: 'npc', target: npcId })
    if (res.detail) {
      const d = res.detail as Record<string, unknown>
      pushNotice(`${d.name}: ${d.occupation} — mood: ${d.emotion}`)
    }
  }

  const handleMapMove = async (x: number, y: number) => {
    const res = await act({ type: 'move', target: { x, y, z: 0 } })
    pushNotice(String(res.message ?? res.error ?? 'Moved'))
  }

  const handleEnter = async (buildingId: number) => {
    const res = await act({ type: 'enter', target: { id: buildingId } })
    pushNotice(String(res.message ?? res.error ?? 'Entered'))
  }

  const handleExit = async () => {
    const res = await act({ type: 'exit' })
    pushNotice(String(res.message ?? res.error ?? 'Exited'))
  }

  const handlePickup = async (itemId: number) => {
    const res = await act({ type: 'pickup', target: itemId })
    pushNotice(String(res.message ?? res.error ?? 'Pickup attempted'))
  }

  const handleUseItem = async (itemId: number) => {
    const res = await act({ type: 'use_item', target: itemId })
    pushNotice(String(res.message ?? res.error ?? 'Use attempted'))
  }

  const handleDropItem = async (itemId: number) => {
    const res = await act({ type: 'drop_item', target: itemId })
    pushNotice(String(res.message ?? res.error ?? 'Drop attempted'))
  }

  const handleGiveItem = async (itemId: number, npcId: number) => {
    const res = await act({ type: 'give', item: itemId, target: npcId })
    pushNotice(String(res.message ?? res.error ?? 'Give attempted'))
  }

  const handleBuy = async (goods: string) => {
    const res = await act({ type: 'buy', goods })
    pushNotice(String(res.message ?? res.error ?? 'Buy attempted'))
  }

  const handleSell = async (itemId: number) => {
    const res = await act({ type: 'sell', item: itemId })
    pushNotice(String(res.message ?? res.error ?? 'Sell attempted'))
  }

  const handlePlant = async (plotId: number, crop: string) => {
    const res = await act({ type: 'plant', target: plotId, crop })
    pushNotice(String(res.message ?? res.error ?? 'Plant attempted'))
  }

  const handleWater = async (plotId: number) => {
    const res = await act({ type: 'water', target: plotId })
    pushNotice(String(res.message ?? res.error ?? 'Water attempted'))
  }

  const handleHarvest = async (plotId: number) => {
    const res = await act({ type: 'harvest', target: plotId })
    pushNotice(String(res.message ?? res.error ?? 'Harvest attempted'))
  }

  const handleFish = async (spotId: number) => {
    const res = await act({ type: 'fish', target: spotId })
    pushNotice(String(res.message ?? res.error ?? 'Fishing attempted'))
  }

  const handleWork = async (jobId: number) => {
    const res = await act({ type: 'work', target: jobId })
    pushNotice(String(res.message ?? res.error ?? 'Work attempted'))
  }

  const handleFocus = (
    target: {
      type: 'building' | 'npc' | 'item'
      id: number
      name: string
      description: string
      sensory?: string[]
      knowledge?: string[]
    } | null,
  ) => {
    setFocusedTarget(target)
  }

  const closeLens = () => setFocusedTarget(null)

  const handleAdvanceTime = async (hours: number) => {
    const res = await act({ type: 'advance_time', hours })
    if (!res.ok) {
      pushNotice(String(res.error ?? 'Time skip failed'))
    } else if (res.message) {
      pushNotice(String(res.message))
    }
  }

  const handleSelectTopic = async (topic: ConversationTopic) => {
    if (!dialogue) return
    // Optimistically append the player's line and a placeholder reply so the
    // panel responds instantly while the LLM is working.
    setDialogue((d) =>
      d
        ? {
            ...d,
            entries: [
              ...d.entries,
              { speaker: 'You', text: topic.label },
              { speaker: d.npcName, text: '…' },
            ],
          }
        : d,
    )
    const res = await act({ type: 'dialogue_topic', target: dialogue.npcId, topic: topic.id })
    const line = res.line as DialogueLine | undefined
    setDialogue((d) => {
      if (!d) return d
      const latest = { speaker: d.npcName, text: line?.text ?? String(res.error ?? '…'), unlocked: line?.knowledge_unlocked ?? [] }
      return {
        ...d,
        entries: [...d.entries.slice(0, -1), latest],
        topics: (res.topics as ConversationTopic[] | undefined) ?? d.topics,
      }
    })
    if (line?.knowledge_unlocked?.length) {
      pushNotice(`Gained knowledge: ${line.knowledge_unlocked.join(', ')}`)
    }
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
          <button onClick={() => setShowJournal(!showJournal)} disabled={!connected}>
            📔 Journal
          </button>
          <button onClick={() => setShowInventory(!showInventory)} disabled={!connected}>
            🎒 Inventory
          </button>
          <button onClick={() => setShowLifestyle(!showLifestyle)} disabled={!connected}>
            🌾 Daily Life
          </button>
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
          <TimePanel state={state} onAdvanceTime={handleAdvanceTime} />
          <PlayerPanel
            state={state}
            act={act}
            onNotice={pushNotice}
            onTalk={handleTalk}
            onInspect={handleInspect}
          />
          <VillagePanel
            state={state}
            selectedNpcId={selectedNpcId}
            onSelectNpc={setSelectedNpcId}
          />
        </aside>

        <section className="column center">
          {state.player && state.player.interior_id > 0 ? (
            <InteriorPanel
              state={state}
              buildingId={state.player.interior_id}
              onTalk={handleTalk}
              onInspect={handleInspect}
              onExit={handleExit}
              onFocus={handleFocus}
            />
          ) : (
            <MapPanel
              state={state}
              onTalk={handleTalk}
              onInspect={handleInspect}
              onMove={handleMapMove}
              onEnter={handleEnter}
              onPickup={handlePickup}
              onPlant={handlePlant}
              onWater={handleWater}
              onHarvest={handleHarvest}
              onFish={handleFish}
              onWork={handleWork}
              onFocus={handleFocus}
            />
          )}
          {selectedNpc ? (
            <NPCDetailPanel state={state} npc={selectedNpc} />
          ) : (
            <div className="panel placeholder">
              <h3>The village breathes</h3>
              <p>
                Use the controls in the left panel: walk to a location, talk to
                someone nearby, or move close to the items left behind by the missing.
              </p>
              <p className="smoke">
                The disappearance of the old miller hangs over every conversation.
              </p>
            </div>
          )}
        </section>

        <aside className="column right">
          {showJournal && (
            <Accordion title="📔 Journal" onClose={() => setShowJournal(false)}>
              <JournalPanel state={state} onClose={() => setShowJournal(false)} />
            </Accordion>
          )}
          {showInventory && (
            <Accordion title="🎒 Inventory" onClose={() => setShowInventory(false)}>
              <InventoryPanel state={state} onUseItem={handleUseItem} onDropItem={handleDropItem} onGiveItem={handleGiveItem} onBuy={handleBuy} onSell={handleSell} onClose={() => setShowInventory(false)} />
            </Accordion>
          )}
          {showLifestyle && (
            <Accordion title="🌾 Daily Life" onClose={() => setShowLifestyle(false)}>
              <LifestylePanel
                state={state}
                onPlant={handlePlant}
                onWater={handleWater}
                onHarvest={handleHarvest}
                onFish={handleFish}
                onWork={handleWork}
              />
            </Accordion>
          )}
          <Accordion title="🔍 Investigation" defaultOpen>
            <InvestigationPanel state={state} />
          </Accordion>
          <Accordion title="🗺️ World">
            <WorldPanel state={state} />
          </Accordion>
        </aside>
      </main>

      <footer className="footer">
        <span>Ashgrove v0.1.0 · C++ simulation server · Web client</span>
        {error && <span className="error-text">{error}</span>}
      </footer>

      {dialogue && (
        <DialoguePanel
          session={dialogue}
          onSelectTopic={handleSelectTopic}
          onClose={() => setDialogue(null)}
        />
      )}
      {focusedTarget && <LensPanel target={focusedTarget} onClose={closeLens} />}
    </div>
  )
}