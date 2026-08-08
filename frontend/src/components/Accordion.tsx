import { useState, type ReactNode } from 'react'

interface AccordionProps {
  title: string
  children: ReactNode
  defaultOpen?: boolean
  onClose?: () => void
}

export function Accordion({ title, children, defaultOpen = false, onClose }: AccordionProps) {
  const [open, setOpen] = useState(defaultOpen)

  return (
    <div className={`panel accordion ${open ? 'accordion-open' : ''}`}>
      <div className="accordion-header" onClick={() => setOpen(!open)}>
        <span className="accordion-title">{title}</span>
        <span className="accordion-toggle">{open ? '▾' : '▸'}</span>
        {onClose && (
          <button
            className="close-btn accordion-close"
            onClick={(e) => {
              e.stopPropagation()
              onClose()
            }}
          >
            ✕
          </button>
        )}
      </div>
      {open && <div className="accordion-body">{children}</div>}
    </div>
  )
}