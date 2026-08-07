// TypeScript types matching the Ashgrove server game protocol.

export const GameMessageType = {
  PlayerAction: 0,
  RequestState: 1,
  DialogueChoice: 2,
  Interact: 3,
  Move: 4,
  UseItem: 5,
  SaveGame: 6,
  LoadGame: 7,
  WorldState: 8,
  DialogueResponse: 9,
  EventNotification: 10,
  SaveResult: 11,
  Error: 12,
  TimeSync: 13,
} as const
export type GameMessageType = (typeof GameMessageType)[keyof typeof GameMessageType]

export interface GameMessage {
  type: GameMessageType
  payload: Record<string, unknown>
}

export interface TimeData {
  ticks: number
  year: number
  day_of_year: number
  hour: number
  minute: number
  season: string
  weather: string
  weather_intensity: number
}

export interface Vec3 {
  x: number
  y: number
  z: number
}

export interface Memory {
  event_id: number
  timestamp: number
  description: string
  participants: number[]
  importance: number
  emotional_valence: number
  is_false: boolean
  confidence: number
  source: string
}

export interface Belief {
  proposition: string
  confidence: number
  formed_at: number
  last_reinforced: number
  evidence: string
  is_core: boolean
}

export interface Relationship {
  target_id: number
  affinity: number
  trust: number
  familiarity: number
  type: string
  last_interaction: number
  history: string[]
}

export interface Goal {
  description: string
  priority: number
  progress: number
  created_at: number
  deadline: number
  status: string
  subgoals: string[]
}

export interface PersonalityTrait {
  name: string
  value: number
}

export interface NPCScheduleEntry {
  start_hour: number
  duration_hours: number
  activity: string
  location_id: number
  companion_id: string
}

export interface NPC {
  id: number
  name: string
  surname: string
  tier: number
  age: number
  gender: string
  occupation: string
  position: Vec3 & { region_id: number }
  health: number
  hunger: number
  fatigue: number
  temperature: number
  injuries: string[]
  illnesses: string[]
  memories: Memory[]
  beliefs: Belief[]
  relationships: Relationship[]
  goals: Goal[]
  personality: PersonalityTrait[]
  current_emotion: number
  emotion_intensity: number
  schedule: NPCScheduleEntry[]
  current_activity: string
  reputation: number
  wealth: number
  home_id: number
  workplace_id: number
  family_ids: number[]
  llm_context: string
  llm_model: string
  needs_llm_update: boolean
}

export interface Building {
  id: number
  name: string
  type: number
  position: { x: number; y: number; z: number; region_id: number }
  owner_id: number
  resident_ids: number[]
  worker_ids: number[]
  condition: number
  value: number
  level: number
  description: string
}

export interface Region {
  id: number
  name: string
  type: number
  building_ids: number[]
  npc_ids: number[]
  connected_region_ids: number[]
  description: string
  resources: Record<string, number>
  danger_level: number
}

export interface Item {
  id: number
  name: string
  category: string
  weight: number
  value: number
  condition: number
  properties: Record<string, string>
  owner_id: number
  container_id: number
}

export interface Knowledge {
  id: number
  category: number
  title: string
  description: string
  completeness: number
  source_npc_ids: number[]
  source_item_ids: number[]
  discovered_at: number
  is_secret: boolean
  unlock_requirement: string
}

export interface Evidence {
  id: number
  name: string
  description: string
  tags: string[]
  reliability: number
  related_npc_id: number
  related_location_id: number
  acquired_from: string
  acquired_at: number
  is_contradictory: boolean
}

export interface World {
  next_entity_id: number
  buildings: Building[]
  regions: Region[]
  items: Item[]
  resource_deposits: unknown[]
  npcs: NPC[]
}

export interface Investigation {
  next_id: number
  knowledge: Knowledge[]
  evidence: Evidence[]
}

export interface PlayerActionRecord {
  tick: number
  verb: string
  summary: string
  result: string
}

export interface PlayerState {
  name: string
  position: Vec3
  region_id: number
  health: number
  hunger: number
  fatigue: number
  reputation: number
  level: number
  xp: number
  inventory: number[]
  resting: boolean
  current_action: string
  action_log: PlayerActionRecord[]
}

export interface DialogueLineData {
  speaker_id: number
  text: string
  affinity_delta: number
  trust_delta: number
  knowledge_unlocked: string[]
  evidence_gained: string[]
  rumor_spread: string[]
}

export interface ConversationTopic {
  id: string
  label: string
  requires_knowledge: string[]
  requires_affinity: number
  available: boolean
}

export interface WorldState {
  time: string
  time_data: TimeData
  world: World
  investigation: Investigation
  player?: PlayerState
}

export const EMOTION_NAMES = [
  'Neutral',
  'Happy',
  'Sad',
  'Angry',
  'Fearful',
  'Disgusted',
  'Surprised',
  'Anxious',
  'Content',
  'Suspicious',
] as const

export const KNOWLEDGE_CATEGORY_NAMES = [
  'History',
  'Folklore',
  'Family',
  'Location',
  'Ritual',
  'Mystery',
  'Mechanics',
  'Relationship',
  'Evidence',
] as const

export const ITEM_CATEGORY_NAMES = [
  'Food',
  'Tool',
  'Weapon',
  'Material',
  'Clothing',
  'Book',
  'Evidence',
] as const