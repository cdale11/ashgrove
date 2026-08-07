import { EMOTION_NAMES } from '../types'
import type { NPC, WorldState } from '../types'

interface VillagePanelProps {
  state: WorldState
  onSelectNpc: (id: number) => void
  selectedNpcId: number | null
}

function emotionName(code: number): string {
  return EMOTION_NAMES[code] ?? 'Unknown'
}

export function VillagePanel({ state, onSelectNpc, selectedNpcId }: VillagePanelProps) {
  const npcs = [...state.world.npcs].sort((a, b) => a.tier - b.tier)

  return (
    <div className="panel village-panel">
      <h3>Villagers ({npcs.length})</h3>
      <ul className="npc-list">
        {npcs.map((npc: NPC) => (
          <li
            key={npc.id}
            className={`npc-entry tier-${npc.tier} ${selectedNpcId === npc.id ? 'selected' : ''}`}
            onClick={() => onSelectNpc(npc.id)}
          >
            <span className="npc-name">{npc.name} {npc.surname}</span>
            <span className="npc-occupation">{npc.occupation}</span>
            <span className="npc-status">
              <span className={`emotion emotion-${npc.current_emotion}`}>{emotionName(npc.current_emotion)}</span>
              <span className="activity">{npc.current_activity}</span>
            </span>
          </li>
        ))}
      </ul>
    </div>
  )
}