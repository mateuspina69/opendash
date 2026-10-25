# Actor layouts on the native-state branch

All files here are test-only. `actor_pools_layout.cpp` visits every slot of all six
pools, including free slots and the eight sub-screen animated slots.
`player_layout.cpp` maps the player, animation channels, idle tracks, pose matrices,
padding lanes and bone-point colours. `classes_*_layout.cpp` contains the named
class/record layouts, grouped by the owning content area. `class_layout.cpp`
dispatches by the exact native class-state type, so a base class never hides its
derived layout. There are 520 class-state types and 572 record layouts at this
branch's headers.

`actor_registry.cpp` retains an owned snapshot for classes constructed lazily by
`classState<T>`. Before replacing a class state it saves the previous occupant,
then loads the new state from the retained image. Pool class-clear intervals are
separate from the rest of the slot. The registry relates native hit boxes, own
collision boxes, scalar-byte targets and short-vector targets to exe addresses;
forward references are repaired when their target class is constructed. Hit-list
contact-box pointers are derived from the registered full hit-box objects.
Actor references to interior blocks encode the offset explicitly (suction,
Blade Arm, Shining Laser, and the guardian beam's corner links).

Loads visit generic fields while excluding the held class's overlapping bytes,
then visit class fields. Work-area addresses are loaded before their contents.
Pointer registration and fence calculation are repeated after loading those
addresses. Partially mapped snapshots keep unmapped native bytes unchanged.

The original OpenDash typed views at `b174b95f` and the current native headers are
the offset references. No executable data or golden
bytes are included in these files.

## Branch limitations for the controller

- `BoneBlock` and `BoneBlockPool` are still image views in the current headers.
  Their matrices, padding/tails, points and scale/semi-transparency bytes already
  remain in the golden-layout image; there is no native block object to copy.
  Native class-owned work records inside those blocks **are** translated, both
  through `BoneBlockArea<T>` and the Tron/elephant/ST4B owned work records.
- `SaveMenuBlock` and other unchanged `MemView` records remain image-backed too.
  The `MemView` overload deliberately leaves those existing records in place.
- `particle_burst_state::BurstParticle::xyWord` and `wordLow` duplicate the bytes
  of `point.x/y` and `word`, but are independent native members in these headers.
  Loads populate both representations; stores use `point` and `word` as the
  canonical bytes. Native code must make these actual aliases (or synchronize
  them during gameplay) to preserve intra-frame reads/drawing.
- `elephant_boss_state::BossFields::flagsLow` similarly duplicates the low byte
  of the slot's `bossFlags`/`flags`. It is loaded from that byte; stores use
  `flags`. Its gameplay reads need a native alias to observe changes to `flags`.
- `GunshipFields::tintBytes_` duplicates `MeshActor::tint` as a separate native
  array. It is loaded, while stores use the mesh tint consumed by drawing. Its
  native reset helper needs to update the mesh tint through a real alias.
- `st0a_fx_state::StageEffectFields::frameLow` refers to the independent `radius`
  member while `frame` refers to the effect slot's trail. The original overlaps
  those bytes. The translator chooses the written representation by the actual
  effect variant (including ST4B's variant+2 class), but cannot fix gameplay
  reads between those native members. That alias belongs in `src/`.

None of these limitations were repaired in `src/`, in accordance with this task's
scope. No configure, build, C++ tests, or commits were run. The controller should
compile and run translator round-trips, the golden suite, and playthroughs.

A source-only audit, which is not a compiler or behavioral test, can be run with:

```
```

It checks native class dispatch coverage, record declaration/definition pairs,
referenced native member names, and quoted include paths.
