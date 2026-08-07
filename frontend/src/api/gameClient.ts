import { GameMessageType } from '../types'
import type { GameMessage, WorldState } from '../types'

type ActionResult = Record<string, unknown>

/**
 * Client for the Ashgrove C++ game server.
 * Uses WebSocket for real-time state, REST for discrete actions.
 */
export class GameClient {
  private ws: WebSocket | null = null
  private listeners = new Set<(state: WorldState) => void>()
  private errorListeners = new Set<(msg: string) => void>()
  private reconnectAttempts = 0
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null
  private connected = false
  private lastState: WorldState | null = null

  connect(): void {
    const proto = window.location.protocol === 'https:' ? 'wss' : 'ws'
    const host = window.location.hostname || 'localhost'
    // The backend WebSocket server runs on the game server port (8000 by
    // default) and binds to all interfaces, so connect directly for LAN
    // access. Vite's WebSocket proxy is avoided to keep multi-host working.
    const wsPort = Number(import.meta.env.VITE_WS_PORT || 8000)
    const wsUrl = `${proto}://${host}:${wsPort}/ws`
    this.connected = true
    this.openSocket(wsUrl)
  }

  private openSocket(url: string) {
    this.ws = new WebSocket(url)

    this.ws.onopen = () => {
      console.log('[game] WebSocket connected')
      this.reconnectAttempts = 0
      // Request current state on connect
      this.requestState()
    }

    this.ws.onmessage = (event) => {
      try {
        const msg = JSON.parse(event.data as string) as GameMessage
        this.handleMessage(msg)
      } catch (e) {
        console.error('[game] failed to parse message', e)
      }
    }

    this.ws.onerror = (err) => {
      console.error('[game] WebSocket error', err)
      this.emitError('WebSocket connection error')
    }

    this.ws.onclose = () => {
      console.log('[game] WebSocket closed')
      this.ws = null
      if (this.connected) {
        this.scheduleReconnect()
      }
    }
  }

  private scheduleReconnect() {
    if (this.reconnectTimer) clearTimeout(this.reconnectTimer)
    const delay = Math.min(1000 * Math.pow(2, this.reconnectAttempts), 10000)
    this.reconnectAttempts++
    this.reconnectTimer = setTimeout(() => {
      this.ws = null
      // Rebuild URL (host may have changed via HMR)
      const proto = window.location.protocol === 'https:' ? 'wss' : 'ws'
      const host = window.location.hostname || 'localhost'
      const wsPort = Number(import.meta.env.VITE_WS_PORT || 8000)
      this.reconnect(`${proto}://${host}:${wsPort}/ws`)
    }, delay)
  }

  private reconnect(url: string) {
    this.openSocket(url)
  }

  private handleMessage(msg: GameMessage) {
    switch (msg.type) {
      case GameMessageType.WorldState:
        this.lastState = msg.payload as unknown as WorldState
        this.notify()
        break
      case GameMessageType.TimeSync:
        this.lastState = msg.payload as unknown as WorldState
        this.notify()
        break
      case GameMessageType.Error:
        this.emitError(String(msg.payload.error ?? 'Unknown error'))
        break
      default:
        break
    }
  }

  /** Fetch current state via REST (fallback / initial load).
   * The game server exposes CORS (Access-Control-Allow-Origin: *), and the
   * Vite dev proxy drops POST bodies, so talk directly to the game server. */
  private restUrl(path: string): string {
    const proto = window.location.protocol === 'https:' ? 'https' : 'http'
    const host = window.location.hostname || 'localhost'
    const port = Number(import.meta.env.VITE_API_PORT || 8000)
    return `${proto}://${host}:${port}${path}`
  }

  async fetchState(): Promise<WorldState> {
    const res = await fetch(this.restUrl('/api/world/state'))
    if (!res.ok) throw new Error(`HTTP ${res.status}`)
    const state = (await res.json()) as WorldState
    this.lastState = state
    return state
  }

  requestState() {
    this.send({ type: GameMessageType.RequestState, payload: {} })
  }

  sendAction(action: Record<string, unknown>) {
    this.send({ type: GameMessageType.PlayerAction, payload: action })
  }

  saveGame() {
    this.send({ type: GameMessageType.SaveGame, payload: {} })
  }

  loadGame() {
    this.send({ type: GameMessageType.LoadGame, payload: {} })
  }

  async actionViaRest(action: Record<string, unknown>): Promise<ActionResult> {
    const res = await fetch(this.restUrl('/api/action'), {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(action),
    })
    if (!res.ok) throw new Error(`HTTP ${res.status}`)
    const data = (await res.json()) as Record<string, unknown>
    // Refresh the full world state so the UI reflects any changes.
    this.fetchState().catch(() => {})
    return data
  }

  private send(msg: GameMessage) {
    if (this.ws && this.ws.readyState === WebSocket.OPEN) {
      this.ws.send(JSON.stringify(msg))
    } else {
      console.warn('[game] WebSocket not open')
    }
  }

  getState(): WorldState | null {
    return this.lastState
  }

  isConnected(): boolean {
    return !!this.ws && this.ws.readyState === WebSocket.OPEN
  }

  onState(cb: (state: WorldState) => void): () => void {
    this.listeners.add(cb)
    // Immediately notify if we already have state
    if (this.lastState) cb(this.lastState)
    return () => this.listeners.delete(cb)
  }

  onError(cb: (msg: string) => void): () => void {
    this.errorListeners.add(cb)
    return () => this.errorListeners.delete(cb)
  }

  private notify() {
    if (this.lastState) {
      this.listeners.forEach((cb) => cb(this.lastState!))
    }
  }

  private emitError(msg: string) {
    this.errorListeners.forEach((cb) => cb(msg))
  }

  disconnect() {
    this.connected = false
    if (this.reconnectTimer) clearTimeout(this.reconnectTimer)
    if (this.ws) this.ws.close()
  }
}