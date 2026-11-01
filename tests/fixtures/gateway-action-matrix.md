# Gateway native action replay matrix

Each scenario is a fresh 42-second guest replay with the same initial LOOK.
The table lists **verified changed bytes only** from native DOS services;
the dispatcher return indicates handling, **not** action success.
No object-specific event effects or uninstrumented writes are claimed.

## baseline

| Guest slot | Typed command | Dispatcher return | Changed object state |
| ---: | --- | --- | --- |
| 18s | open drawer | 0x1 | set(0x124,0x0c), set(0x124,0x02), set(0x123,0x07) |
| 21s | look | 0x1 | — |
| 25s | take book | 0x1 | location-set(0x123,0xd3), set(0x123,0x02) |
| 28s | inventory | 0x1 | — |
| 31s | drop book | 0x1 | location-set(0x123,0xee) |
| 34s | close drawer | 0x1 | clear(0x124,0x0c) |
| 37s | look | 0x1 | — |

Full trace: `baseline/native.trace`

## repeat

| Guest slot | Typed command | Dispatcher return | Changed object state |
| ---: | --- | --- | --- |
| 18s | open drawer | 0x1 | set(0x124,0x0c), set(0x124,0x02), set(0x123,0x07) |
| 21s | close drawer | 0x1 | clear(0x124,0x0c) |
| 25s | open drawer | 0x1 | set(0x124,0x0c) |
| 28s | take book | 0x1 | location-set(0x123,0xd3), set(0x123,0x02) |
| 31s | drop book | 0x1 | location-set(0x123,0xee) |
| 34s | take book | 0x1 | location-set(0x123,0xd3) |
| 37s | inventory | 0x1 | — |

Full trace: `repeat/native.trace`

## open-twice

| Guest slot | Typed command | Dispatcher return | Changed object state |
| ---: | --- | --- | --- |
| 18s | open drawer | 0x1 | set(0x124,0x0c), set(0x124,0x02), set(0x123,0x07) |
| 21s | open drawer | 0x1 | — |
| 25s | close drawer | 0x1 | clear(0x124,0x0c) |
| 28s | close drawer | 0x1 | — |
| 31s | open drawer | 0x1 | set(0x124,0x0c) |
| 34s | close drawer | 0x1 | clear(0x124,0x0c) |
| 37s | inventory | 0x1 | — |

Full trace: `open-twice/native.trace`

## close-first

| Guest slot | Typed command | Dispatcher return | Changed object state |
| ---: | --- | --- | --- |
| 18s | close drawer | 0x1 | — |
| 21s | open drawer | 0x1 | set(0x124,0x0c), set(0x124,0x02), set(0x123,0x07) |
| 25s | close drawer | 0x1 | clear(0x124,0x0c) |
| 28s | open drawer | 0x1 | set(0x124,0x0c) |
| 31s | open drawer | 0x1 | — |
| 34s | close drawer | 0x1 | clear(0x124,0x0c) |
| 37s | inventory | 0x1 | — |

Full trace: `close-first/native.trace`

## take-twice

| Guest slot | Typed command | Dispatcher return | Changed object state |
| ---: | --- | --- | --- |
| 18s | open drawer | 0x1 | set(0x124,0x0c), set(0x124,0x02), set(0x123,0x07) |
| 21s | take book | 0x1 | location-set(0x123,0xd3), set(0x123,0x02) |
| 25s | take book | (no dispatch observed) | — |
| 28s | drop book | 0x1 | location-set(0x123,0xee) |
| 31s | drop book | (no dispatch observed) | — |
| 34s | take book | 0x1 | location-set(0x123,0xd3) |
| 37s | inventory | 0x1 | — |

Full trace: `take-twice/native.trace`

## take-before-open

| Guest slot | Typed command | Dispatcher return | Changed object state |
| ---: | --- | --- | --- |
| 18s | take book | (no dispatch observed) | — |
| 21s | open drawer | 0x1 | set(0x124,0x0c), set(0x124,0x02), set(0x123,0x07) |
| 25s | take book | 0x1 | location-set(0x123,0xd3), set(0x123,0x02) |
| 28s | drop book | 0x1 | location-set(0x123,0xee) |
| 31s | take book | 0x1 | location-set(0x123,0xd3) |
| 34s | drop book | 0x1 | location-set(0x123,0xee) |
| 37s | inventory | 0x1 | — |

Full trace: `take-before-open/native.trace`
