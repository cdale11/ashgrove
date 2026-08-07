import { useEffect, useState, useCallback } from 'react'
import { GameClient } from '../api/gameClient'
import type { WorldState } from '../types'

let sharedClient: GameClient | null = null

export function getGameClient(): GameClient {
  if (!sharedClient) {
    sharedClient = new GameClient()
    sharedClient.connect()
  }
  return sharedClient
}

export function useGameState() {
  const client = getGameClient()
  const [state, setState] = useState<WorldState | null>(client.getState())
  const [connected, setConnected] = useState(client.isConnected())
  const [error, setError] = useState<string | null>(null)

  useEffect(() => {
    const offState = client.onState((s) => {
      setState(s)
      setConnected(client.isConnected())
    })
    const offError = client.onError((msg) => setError(msg))

    // Poll connectivity (WebSocket onopen/onclose events aren't exposed via state)
    const interval = setInterval(() => {
      setConnected(client.isConnected())
    }, 2000)

    // Initial load fallback
    client.fetchState().then(setState).catch(() => {
      // Backend may not be running; the WS reconnect loop will pick it up
    })

    return () => {
      offState()
      offError()
      clearInterval(interval)
    }
  }, [client])

  const sendAction = useCallback(
    (action: Record<string, unknown>) => {
      client.sendAction(action)
    },
    [client],
  )

  const saveGame = useCallback(() => client.saveGame(), [client])
  const loadGame = useCallback(() => client.loadGame(), [client])

  return { state, connected, error, sendAction, saveGame, loadGame }
}