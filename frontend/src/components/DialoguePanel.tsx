import type { ConversationTopic } from '../types'

export interface DialogueEntry {
  speaker: string
  text: string
  unlocked?: string[]
}

export interface DialogueSession {
  npcId: number
  npcName: string
  entries: DialogueEntry[]
  topics: ConversationTopic[]
}

interface DialoguePanelProps {
  session: DialogueSession
  onSelectTopic: (topic: ConversationTopic) => void
  onClose: () => void
}

export function DialoguePanel({ session, onSelectTopic, onClose }: DialoguePanelProps) {
  return (
    <div className="dialogue-overlay">
      <div className="dialogue-panel">
        <div className="dialogue-header">
          <span className="dialogue-npc">{session.npcName}</span>
          <button className="dialogue-close" onClick={onClose}>✕</button>
        </div>
        <div className="dialogue-transcript">
          {session.entries.map((e, i) => (
            <div key={i} className={`dialogue-line dialogue-${e.speaker === session.npcName ? 'npc' : 'player'}`}>
              <span className="dialogue-speaker">{e.speaker}</span>
              <span className="dialogue-text">{e.text}</span>
              {e.unlocked && e.unlocked.length > 0 && (
                <span className="dialogue-unlock">+ {e.unlocked.join(', ')}</span>
              )}
            </div>
          ))}
        </div>
        <div className="dialogue-topics">
          {session.topics.filter((t) => t.available).map((t) => (
            <button key={t.id} className="topic-btn" onClick={() => onSelectTopic(t)}>
              {t.label}
            </button>
          ))}
        </div>
      </div>
    </div>
  )
}