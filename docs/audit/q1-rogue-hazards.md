# Rogue hazards

Native Rogue earthquake, buzzsaw and lightning-trail callbacks now use shared
actors, physics, path ownership, damage and the existing three-ray lightning
helper. Earthquake, team/rune handling and ending remain separate stages so
the application can preserve their source order across selected game providers.

An independent reviewer read the complete new hazards and electric code, every
integration change, the full donor hazards, ending path and after-physics hooks.
Review found a body-reader callback could retire a path follower before its next
read; split reads now revalidate both exact generations. The fix was reread.
Root matched the final seven-file hashes before integrating the packet.

Particle events preserve the raw count in `value` and a checked ceiling of the
positive iteration count in `count`. The presentation consumer must test raw
`value == 1024` for the explosion sentinel: `1023.5` means 1024 ordinary particles.
This application obligation is handed off and remains open, along with private
checkpoint and selected-control integration. No engine execution ran.
