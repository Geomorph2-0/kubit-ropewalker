# Designing a Gear Drive for a Rope-Walking Robot in Linkage 3.16
### (Updated to the final Hoeken-linkage design)

This tutorial takes you from the basics of the Linkage program to a working, simulated gear-and-crank drivetrain for a rope-walking robot. It reflects the *final, tuned* design: a 1:2 gear reduction driving a **Hoeken straight-line linkage**, whose hand-hook rides the rope along a flat, near-constant-speed grip stroke for 61% of every crank revolution.

**How the robot works:** it hangs below a taut rope. A motor spins a gear pair; the output gear carries a crank pin driving a straight arm constrained by a rocker. The arm's far end (the hand, with a hook) traces a lens-shaped path: a **flat bottom segment** where the hook rides the rope and pulls the robot along, and a **bulge on top** where the hook lifts off, swings back, and drops onto the rope again. Two arms 180° out of phase alternate grips with overlap, so one hand is always on the rope.

**Final dimensions (mm):**

| Element | Value |
|---|---|
| Motor gear axis **C** | (0, −50), rotating input, 15 RPM clockwise |
| Driven gear axis **F** | (40, −50), plain anchor |
| Gear ratio | 1:2 (output 7.5 RPM) |
| Crank pin **D** | on the driven gear, 25 mm from F |
| Rocker anchor **E** | (90, −50) — ground link F–E = 50 = 2×crank |
| Coupler D–**G** | 62.5 = 2.5×crank |
| Rocker E–G | 62.5 |
| Arm extension G–**H** | 62.5, with D–G–H collinear (G = midpoint of a straight bar) |
| Rope | horizontal at y ≈ 50, the flat bottom of H's path |

These are the classic **Hoeken proportions** (crank : ground : coupler : rocker = 1 : 2 : 2.5 : 2.5, trace point on the straight coupler extension). Deviating even 2% from the 2.5 ratio shrinks the flat grip phase dramatically (62.5 → 61.3 drops it from 61% to 28% of the cycle), so enter lengths exactly.

---

## Part 0 — Setup and Orientation (5 minutes)

1. **Install and run Linkage** (`linkage.msi`). A new mechanism opens with one anchor.
2. **Three core elements:** **Connector** — a joint (circle); **Link** — a rigid bar joining two or more connectors; **Anchor** — a connector fixed to the invisible ground; a *rotating input anchor* is an anchor with a motor.
3. **Essential controls:** right-click empty space → popup **element gallery** (how you insert things); **R** runs/stops the simulation; **Ctrl+Z** undoes; scroll = zoom, right-drag = pan; toolbar **Details → Labels** on (keep them on); units = **Millimeters**.
4. **Precise placement:** select one connector and the toolbar's **dimension text box** shows/sets its `x,y`. Select two connectors and it shows/sets the distance. Select two gears and it sets the ratio (`1:2`). This box is how everything below is entered exactly.

## Part 1 — Warm-Up: The Four-Bar (10 minutes)

Skip if you've used Linkage before.

1. Delete the starting anchor. Right-click → insert the **link with a rotating input anchor** (6th element, top row).
2. Right-click below-right → insert the **link with a plain anchor** (5th element).
3. Click the motor link's free connector, **Shift+click** the other link's free connector, press **L** (Link). You have a four-bar.
4. Drag connectors until the coupler is more than twice the crank's length, press **R**. Watch it run; red error marks mean the geometry binds — adjust and re-run. This select-then-operate rhythm, and red-marks-mean-binding, is all of Linkage in miniature.

Delete everything (Ctrl+A, Delete) before continuing.

## Part 2 — The Gear Drivetrain (15 minutes)

1. Right-click → element gallery → **Gear** (circle-with-center icon, middle row — not the ⊕ drawing circle in the bottom row, which is decoration and can't mesh). A shaded circle appears; that's a gear.
2. Click empty space to deselect. Click the gear's **center connector** → **Properties** → **Rotating Input Anchor, 15 RPM, Clockwise** → OK. Set its location: with the center selected, type `0,-50` in the dimension box, Enter.
3. Insert a second **Gear** to the right. Set its center to a plain **Anchor**, location `40,-50`.
4. **Mesh with a ratio:** click the motor gear's circle, Shift+click the driven gear's circle (the *second* selected shows a **red** rectangle — order matters), type `1:2` in the dimension box, Enter. The gears redraw touching: radii 13.33 and 26.67 (Linkage sizes gears from ratio + center distance — you never set diameters directly).
5. Press **R**: motor gear 15 RPM, driven gear 7.5 RPM opposite direction, rims rolling together. Stop.

Gear facts worth knowing: no ratio set = just overlapping circles that ignore each other; a grey dashed circle around a gear when you select its partner confirms the mesh; gears resize automatically if you move one center (they keep touching at the set ratio), so move meshed gears **as a pair** to reposition without resizing.

## Part 3 — Crank and Straight Arm (20 minutes)

In Linkage a **gear is a link** and can carry extra connectors — the cleanest crank.

1. **Crank pin:** click the **driven gear**, press **A** (Add) — a new connector appears on the gear. Select it, then Shift+click the gear's center; type `25` in the dimension box to set the crank radius exactly. For a clean starting pose, put the pin at `65,-50` (3 o'clock): select just the pin and type the coordinates. This connector is **D**.
2. **Rocker anchor:** right-click → insert a **link with an anchor and a connector**. Select its anchor, type `90,-50`. This anchor is **E**.
3. **The straight arm:** right-click → insert a **link with three connectors** (triangle element). You'll straighten it into a bar D–G–H:
   - Drag one corner near the crank pin, then **Join** it to D: click the corner, Shift+click **D** (D *last* — Join collapses everything onto the last-selected connector), press **J**.
   - Select the middle connector, type `77.5,11.24` — this is **G**.
   - Select the last connector, type `90,72.47` — this is **H**, the hand. D, G, H are now exactly collinear with G at the midpoint (all three gaps 62.5).
4. **Close the rocker:** drag the E-link's free connector near G, then Join: click it, Shift+click **G**, press **J**. The dimension between E and G should read 62.5.
5. **Rope:** insert a **Guideline** from the gallery's drawing elements. Set each endpoint's coordinates via the dimension box to y = `50` (e.g. `20,50` and `160,50`). The guideline is a visual reference only — nothing simulates against it; the flat bottom of the hand's path defines rope height, not vice versa.
6. **Watch the path:** select **H** → Properties → check **Draw Motion Path** → OK. Press **R**. H traces a wide lens: flat bottom sliding along y = 50 (grip), bulge up to y ≈ 72 (release/return). Escape clears the path; red marks anywhere mean a length was mistyped — re-check the table.

Why this shape is right: the hook hangs **over** the rope, so the grip phase is the flat *bottom* — gravity seats the hook while it pulls — and the top bulge is the lift-off-and-return. The Hoeken proportions make the grip segment straight to within 0.5 mm and near-constant speed for ~61% of the revolution.

## Part 4 — Second Arm, Synchronized and Out of Phase (15 minutes)

1. Insert a third **Gear**, center = plain **Anchor** at `130,-50`.
2. Select the **driven gear first**, Shift+click the new gear → **Align → Ratio…** → choose **Chain / Belt** → `1:1`. Dotted lines show the belt; chain-linked gears turn the **same direction** (meshed gears would reverse).
3. Add a crank pin to the new gear (select gear, **A**, set 25 mm from its center) — but place it at `105,-50` (9 o'clock), i.e. **180° opposite** the first crank's pose. That phase offset is the alternating gait.
4. Duplicate the arm assembly: select the first arm and rocker (Ctrl+click each), **Ctrl+D**, drag the copies over by 90 mm, then Join the copy's tail to the new crank pin and its rocker anchor to `180,-50`.
5. Enable Draw Motion Path on the second hand, run: two identical lens paths, one hand always in its flat grip while the other returns — with a ~22% overlap where both grip (61% + 61% − 100%).

## Part 5 — Verify, Measure, Export (10 minutes)

- **Auto Dimensions** (D key): shows all lengths, and gear RPMs during simulation — confirm 15 in / 7.5 out.
- **Measurement line** (gallery): with two connectors pre-selected it fastens to them and updates live — useful for hand-to-rope distance through the cycle.
- **Exports** (File menu): **Animation** → AVI (install the x264vfw codec, one-pass mode); **Motion Path** → CSV of H's coordinates and speed at 1/30 s steps (analyze the grip stroke in a spreadsheet); **Parts List** button → every link laid flat with dimensions (your cut list); Printing tab → **Actual Size** for 1:1 link templates (Print Preview first).

## Troubleshooting Quick Reference

| Symptom | Likely cause | Fix |
|---|---|---|
| Red marks / "Unable to simulate" | A length is off — Hoeken has little margin | Re-enter the exact table values |
| Gears don't turn together | No ratio set | Select both, type `1:2` |
| Gears ballooned in size | You moved one center; sizes follow center distance | Move meshed gears as a pair |
| Second gear spins the wrong way | Meshed instead of chained | Align → Ratio → Chain/Belt |
| Grip phase shrank badly | Coupler/rocker not exactly 2.5× crank | Set all three long links to 62.5 |
| Ratio applied to wrong gear | Selection order reversed (red box = second) | Reselect in order |
| Path vanished | Edits invalidate paths | Re-run the simulation |

## Where to Go Next

- **CAD and build:** see the companion document *From Linkage to Onshape* for the full parametric 3D rebuild, printable parts, and an animated assembly.
- **Perfect the handover:** the ~17% speed variation across the grip stroke causes slight hook scuff during the two-hand overlap. A closed **spline cam** (fasten a spline to the output, sliding-connector follower) can give a true constant-velocity dwell if you ever want to eliminate it.
- **Compact drivetrain:** Linkage simulates internal/planetary gear sets if you want the reduction smaller than a 26.7 mm gear allows.
