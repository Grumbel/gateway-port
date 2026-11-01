# Gateway native action replay matrix

Each scenario is a fresh 42-second guest replay with the same initial LOOK.
The table lists **verified changed bytes only** from native DOS services;
the dispatcher return indicates handling, **not** action success.
No object-specific event effects or uninstrumented writes are claimed.

## closed-after-reveal

| Guest slot | Typed command | Dispatcher return | Changed object state |
| ---: | --- | --- | --- |
| 18s | open drawer | 0x1 | set(0x124,0x0c), set(0x124,0x02), set(0x123,0x07) |
| 21s | close drawer | 0x1 | clear(0x124,0x0c) |
| 25s | take book | (no dispatch observed) | — |
| 28s | open drawer | 0x1 | set(0x124,0x0c) |
| 31s | take book | 0x1 | location-set(0x123,0xd3), set(0x123,0x02) |
| 34s | drop book | 0x1 | location-set(0x123,0xee) |
| 37s | look | 0x1 | — |

Full trace: `closed-after-reveal/native.trace`

### Earlier selector stage (raw observations)

| Guest slot | Command | Selector calls (raw AX) | Final dispatch |
| ---: | --- | --- | --- |
| 18s | open drawer | no callback AX=0x0 | observed |
| 21s | close drawer | no callback AX=0x0 | observed |
| 25s | take book | — | not observed |
| 28s | open drawer | no callback AX=0x0 | observed |
| 31s | take book | selector=0x08 -> 2c24:1939 AX=0x0 | observed |
| 34s | drop book | selector=0x04 -> 2c24:1961 AX=0x0 | observed |
| 37s | look | no callback AX=0x0 | observed |

AX is an unclassified callback result, not a proven permission bit.
Input/trace association is by guest time slot only.

## room-after-close

| Guest slot | Typed command | Dispatcher return | Changed object state |
| ---: | --- | --- | --- |
| 18s | open drawer | 0x1 | set(0x124,0x0c), set(0x124,0x02), set(0x123,0x07) |
| 21s | take book | 0x1 | location-set(0x123,0xd3), set(0x123,0x02) |
| 25s | close drawer | 0x1 | clear(0x124,0x0c) |
| 28s | drop book | 0x1 | location-set(0x123,0xee) |
| 31s | take book | 0x1 | location-set(0x123,0xd3) |
| 34s | inventory | 0x1 | — |
| 37s | look | 0x1 | — |

Full trace: `room-after-close/native.trace`

### Earlier selector stage (raw observations)

| Guest slot | Command | Selector calls (raw AX) | Final dispatch |
| ---: | --- | --- | --- |
| 18s | open drawer | no callback AX=0x0 | observed |
| 21s | take book | selector=0x08 -> 2c24:1939 AX=0x0 | observed |
| 25s | close drawer | no callback AX=0x0 | observed |
| 28s | drop book | selector=0x04 -> 2c24:1961 AX=0x0 | observed |
| 31s | take book | selector=0x08 -> 2c24:1939 AX=0x0 | observed |
| 34s | inventory | no callback AX=0x0 | observed |
| 37s | look | no callback AX=0x0 | observed |

AX is an unclassified callback result, not a proven permission bit.
Input/trace association is by guest time slot only.

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

### Earlier selector stage (raw observations)

| Guest slot | Command | Selector calls (raw AX) | Final dispatch |
| ---: | --- | --- | --- |
| 18s | open drawer | no callback AX=0x0 | observed |
| 21s | take book | selector=0x08 -> 2c24:1939 AX=0x0 | observed |
| 25s | take book | selector=0x08 -> 2c24:1939 AX=0x1 | not observed |
| 28s | drop book | selector=0x04 -> 2c24:1961 AX=0x0 | observed |
| 31s | drop book | selector=0x04 -> 2c24:1961 AX=0x1 | not observed |
| 34s | take book | selector=0x08 -> 2c24:1939 AX=0x0 | observed |
| 37s | inventory | no callback AX=0x0 | observed |

AX is an unclassified callback result, not a proven permission bit.
Input/trace association is by guest time slot only.
