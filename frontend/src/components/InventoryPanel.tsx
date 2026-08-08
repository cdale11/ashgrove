import { useState } from 'react'
import type { WorldState, Item } from '../types'

interface InventoryPanelProps {
  state: WorldState
  onUseItem: (itemId: number) => void
  onDropItem: (itemId: number) => void
  onGiveItem: (itemId: number, npcId: number) => void
  onBuy: (goods: string) => void
  onSell: (itemId: number) => void
  onClose: () => void
}

const CATEGORY_ICONS: Record<string, string> = {
  food: '🍞',
  tool: '🔨',
  weapon: '⚔️',
  material: '🪵',
  clothing: '🧥',
  book: '📜',
  evidence: '🔍',
  seed: '🌱',
}

const CATEGORY_COLORS: Record<string, string> = {
  food: '#27ae60',
  tool: '#7f8c8d',
  weapon: '#c0392b',
  material: '#8b7355',
  clothing: '#8e44ad',
  book: '#2980b9',
  evidence: '#f1c40f',
  seed: '#2ecc71',
}

export function InventoryPanel({ state, onUseItem, onDropItem, onGiveItem, onBuy, onSell, onClose }: InventoryPanelProps) {
  const [selectedItem, setSelectedItem] = useState<Item | null>(null)
  const [giveNpcId, setGiveNpcId] = useState<number | null>(null)
  const player = state.player

  const inventoryItems: Item[] = (player?.inventory ?? [])
    .map((id) => state.world.items.find((i) => i.id === id))
    .filter((i): i is Item => i !== undefined)

  const nearbyNpcs = state.world.npcs.filter(
    (n) => n.position.region_id === player?.region_id,
  )

  // Trade is only available near Ingrid, the shopkeeper. The frontend mirrors the
  // server's proximity rule (15 units) so the UI can hide/grey the buttons.
  const shopkeeper = nearbyNpcs.find((n) => n.occupation === 'Shopkeeper')
  const dx = (player?.position?.x ?? 0) - (shopkeeper?.position.x ?? 0)
  const dy = (player?.position?.y ?? 0) - (shopkeeper?.position.y ?? 0)
  const nearShop = shopkeeper !== undefined && Math.hypot(dx, dy) <= 15

  const totalWeight = inventoryItems.reduce((sum, i) => sum + (i.weight ?? 0), 0)
  const maxWeight = 50 // TODO: base on strength

  return (
    <div className="panel inventory-panel">
      <div className="inventory-header">
        <h3>🎒 Inventory</h3>
        <div className="weight-display">
          {totalWeight.toFixed(1)} / {maxWeight} kg
          <div className="weight-bar">
            <div className="weight-fill" style={{ width: `${Math.min(100, (totalWeight / maxWeight) * 100)}%` }} />
          </div>
        </div>
        <button className="close-btn" onClick={onClose}>✕</button>
      </div>

      <div className="coin-display">
        {Math.floor(player?.money ?? 0)} 🪙 coins
      </div>

      {inventoryItems.length === 0 ? (
        <p className="empty-state">Your pack is empty. Explore the village and pick up items you find.</p>
      ) : (
        <div className="inventory-grid">
          {inventoryItems.map((item) => (
            <div
              key={item.id}
              className={`inventory-item ${selectedItem?.id === item.id ? 'selected' : ''}`}
              onClick={() => setSelectedItem(selectedItem?.id === item.id ? null : item)}
            >
              <div className="item-icon" style={{ borderColor: CATEGORY_COLORS[item.category] ?? '#555' }}>
                {CATEGORY_ICONS[item.category] ?? '📦'}
              </div>
              <div className="item-info">
                <span className="item-name">{item.name}</span>
                <span className="item-category">{item.category}</span>
              </div>
              <div className="item-stats">
                <span className="item-weight">{item.weight?.toFixed(1) ?? '?'}kg</span>
                <span className="item-value">{Math.floor(item.value ?? 0)}💰</span>
              </div>
              {item.condition !== undefined && (
                <div className="item-condition">
                  <div className="condition-bar">
                    <div className="condition-fill" style={{ width: `${Math.round((item.condition ?? 1) * 100)}%` }} />
                  </div>
                </div>
              )}
            </div>
          ))}
        </div>
      )}

      {selectedItem && (
        <div className="item-detail">
          <h4>{selectedItem.name}</h4>
          <p className="item-desc">{selectedItem.properties?.description ?? 'No description available.'}</p>
          <div className="item-props">
            <span>Category: {selectedItem.category}</span>
            <span>Weight: {selectedItem.weight?.toFixed(1) ?? '?'} kg</span>
            <span>Value: {selectedItem.value ?? 0}</span>
            <span>Condition: {Math.round((selectedItem.condition ?? 1) * 100)}%</span>
          </div>
          {Object.keys(selectedItem.properties ?? {}).length > 0 && (
            <div className="item-properties">
              <h5>Properties</h5>
              {Object.entries(selectedItem.properties ?? {}).map(([key, value]) => (
                <span key={key} className="property-tag">{key}: {value}</span>
              ))}
            </div>
          )}
          <div className="item-actions">
            <button onClick={() => onUseItem(selectedItem.id)}>Use</button>
            <button className="danger" onClick={() => onDropItem(selectedItem.id)}>Drop</button>
            <button
              onClick={() => setGiveNpcId(giveNpcId === null ? 0 : null)}
              className={giveNpcId !== null ? 'active' : ''}
            >
              Give
            </button>
            <button
              className={nearShop ? 'active' : ''}
              disabled={!nearShop}
              onClick={() => onSell(selectedItem.id)}
            >
              Sell
            </button>
          </div>
          {giveNpcId !== null && (
            <div className="give-targets">
              <label>To whom?</label>
              {nearbyNpcs.length === 0 && <span className="empty-state">No one is nearby.</span>}
              {nearbyNpcs.map((n) => (
                <button
                  key={n.id}
                  onClick={() => {
                    onGiveItem(selectedItem.id, n.id)
                    setGiveNpcId(null)
                    setSelectedItem(null)
                  }}
                >
                  {n.name} {n.surname}
                </button>
              ))}
            </div>
          )}
        </div>
      )}

      <div className="shop-section">
        <div className="shop-header">
          <span>🛒 Ingrid's Goods</span>
          {nearShop ? <span className="shop-open tag-good">OPEN</span> : <span className="shop-open tag-muted">out of earshot</span>}
        </div>
        {nearShop ? (
          <div className="shop-list">
            {(state.shop_catalog ?? []).map((entry) => (
              <div key={entry.key} className="shop-item">
                <span className="shop-item-name">
                  {CATEGORY_ICONS[entry.category] ?? '📦'} {entry.name}
                </span>
                <span className="shop-item-price">{entry.price}🪙</span>
                <button
                  disabled={Math.floor(player?.money ?? 0) < entry.price}
                  onClick={() => onBuy(entry.key)}
                >
                  Buy
                </button>
              </div>
            ))}
          </div>
        ) : (
          <p className="shop-hint">Walk over to Ingrid's shop near the village square to trade.</p>
        )}
      </div>
    </div>
  )
}