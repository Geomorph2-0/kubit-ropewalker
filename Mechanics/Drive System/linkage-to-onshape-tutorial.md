# From Linkage to Onshape: Building the Rope-Walker Drive System in CAD
### A click-by-click guide for first-time Onshape users

This tutorial rebuilds your simulated 2D mechanism as a real, parametric, animatable 3D model in Onshape, assuming **zero prior Onshape experience**. Every non-obvious click is spelled out. Where a concept exists in the official docs, the relevant Help section name is given so you can search it at cad.onshape.com/help.

## The Design You're Transferring (final numbers)

| Element | Value | Notes |
|---|---|---|
| Motor gear axis **C** | (0, −50) | position flexible — see §2.2 |
| Driven gear axis **F** | (40, −50) | fixed frame point |
| Rocker post **E** | (90, −50) | ground link F–E = 50 |
| Crank radius F–**D** | 25 | pin on the driven gear |
| Coupler D–**G** | **62.5** | use 62.5, not 61.29 — restores the 61% grip |
| Rocker E–G | **62.5** | |
| Arm extension G–**H** | **62.5** | D, G, H collinear; G is the arm's midpoint |
| Gear ratio | 1 : 2 (20T : 40T) | 15 RPM in → 7.5 RPM at the crank |

---

# Part 0 — Onshape Survival Kit (15 min)

Read this once; everything later depends on it.

## 0.1 The interface

- **Tabs (bottom bar):** an Onshape *document* contains multiple tabs — Part Studios (where geometry is modeled), Assemblies (where parts are mated and moved), Variable Studios, Drawings. The **+** button at bottom-left creates new tabs. You already have `Part Studio 1`, `Dimensions`, and `Assembly 1`.
- **Feature list (left panel, in a Part Studio):** a chronological recipe of everything you've done — sketches, extrudes, planes. You can right-click any feature to **Edit**, **Rename**, **Suppress**, or **Delete**. Onshape is *history-based*: editing an early feature regenerates everything after it.
- **Default geometry:** every Part Studio starts with an **Origin** and three planes — **Top**, **Front**, **Right** — listed at the top of the feature list. You'll sketch on Front.
- **Toolbar (top):** in a Part Studio: Sketch, Extrude, Revolve, Plane, Fillet… In an Assembly: Insert, Mate, Relations, Animate tools.
- **View cube (top-right):** click faces/corners to orient. Middle-mouse-drag rotates, right-mouse-drag pans (or Ctrl+right-drag), scroll zooms. **Press `f`** to zoom-to-fit if you get lost.

## 0.2 Check your units

Click the **☰ hamburger menu** (top-left, next to the document name) → **Workspace units** → set **Length: Millimeter**. Everything in this tutorial assumes mm.

## 0.3 The five habits of Onshape sketching

*(Help: "Sketch tools", "Sketch constraints", "Dimension")*

1. **Blue = under-defined, black = fully defined.** Blue sketch entities can still be dragged; black ones are locked by dimensions/constraints. Your goal in nearly every sketch: everything black except things you deliberately leave free.
2. **Dimensions are unsigned.** You can't type −50. Instead, *draw the geometry on the intended side* (e.g., below the origin) and dimension its distance as 50. The sketch solver keeps it on the side where you drew it.
3. **Dimension tool = `d`.** Press `d`, click one or two entities, move the mouse, click to place the dimension text, type the value, Enter. When dimensioning **between two points**, where you place the text matters: drag the text *directly below/above* the points and you get the **horizontal distance**; drag it *to the side* and you get the **vertical distance**; diagonal placement gives the straight-line distance. This is how you'll set x and y separately.
4. **Variables in dimensions.** In any dimension value box, type `#` — an autocomplete list of your Variable Studio variables pops up. Pick `#crank`, or type expressions like `#pinDia + 2*#clear`.
5. **Constraints via selection.** Click an entity, Ctrl+click a second — a row of applicable constraint icons appears in the toolbar (and in the right-click menu): Coincident, Horizontal, Vertical, Midpoint, Collinear, Tangent, Equal. Hover any icon for its name. To delete a constraint: click the entity, look at the small constraint glyphs next to it, click one and press Delete.

Three more tool facts you'll need:

- **Construction geometry:** select any sketch entity and press **`q`** (or click the construction icon — dashed-line symbol). Construction entities guide and constrain but don't create solid material. Used here for gear pitch circles and the skeleton.
- **Normal view:** press **`n`** while a sketch is open to look flat-on at the sketch plane. Do this immediately every time you open a sketch.
- **Use/Project tool:** inside a sketch, this tool (icon: an edge with a projection arrow, in the sketch toolbar) lets you click geometry from *another* sketch or a part face, and copies it into the current sketch as a reference that stays linked. **This is the single most important tool in this tutorial** — it's how every part inherits its position from the skeleton instead of you re-typing coordinates.

## 0.4 Extrude essentials (regions and direction)

- **Regions:** any enclosed area in a sketch is a "region," and extruding means clicking the specific regions you want as material. Curves subdivide areas: a rectangle with circles inside yields one plate region plus each circle's interior. Holes exist by *not selecting* the circle regions — no separate cut needed. If nothing shades when you hover, the outline isn't closed: find the gap and snap the endpoints together.
- **Direction:** the yellow preview is the truth. Never accept an extrude (or offset plane) while looking flat-on — rotate a few degrees first and confirm the preview grows to the intended side. To flip: click the **small two-arrow toggle next to the Depth field**, or grab the preview's drag-arrow in the graphics area and pull it through the plane.
- **Trap:** the checkbox labeled **"Direction"** in the Extrude dialog is a *different* option (extrude along a chosen edge/axis). Leave it unchecked; the flip toggle by Depth is what you want.
- **Merge scope:** Add/Remove extrudes default to affecting broad scopes — always set Merge scope to the one intended part, especially for Remove (or you'll drill everything behind your hole).

## 0.5 Escape hatches

- **Esc** exits the current tool. Clicking the green ✓ in a dialog commits; the red ✗ cancels.
- **Ctrl+Z** undoes, and the feature list means nothing is ever unrecoverable — you can always edit or delete a feature.

---

# Part 1 — Variable Studio (10 min)

*(Help: "Variable Studio")*

You've already started this — finish the table. In the `Dimensions` tab, type each name into the empty **Name** row at the bottom; a new row appears as you go. Names are entered **without** the `#` — the `#` prefix is only used when *referencing* a variable in a dimension field.

| Name | Variable type | Value | Description |
|---|---|---|---|
| crank | Length | 25 mm | F→D crank radius |
| ground | Length | 50 mm | F→E fixed link (Hoeken 2×crank) |
| coupler | Length | 62.5 mm | = rocker = arm half-length (Hoeken 2.5×crank) |
| module | Length | 1.333 mm | gear module, see §2.2 |
| z1 | Number | 20 | motor gear teeth (no units) |
| z2 | Number | 40 | driven gear teeth (no units) |
| thk | Length | 4 mm | link/gear thickness |
| pinDia | Length | 4 mm | joint pin diameter |
| clear | Length | 0.25 mm | printed clearance per side |
| ropeDia | Length | 4 mm | rope/cord diameter |
| layerGap | Length | 0.5 mm | washer gap between moving layers |

Details that matter:

- **Variable type:** everything physical is **Length** (Onshape appends mm automatically). Tooth counts `z1`/`z2` are dimensionless — set their type to **Number** using the dropdown, or expressions like `#module*#z1/2` will produce unit errors.
- **Description column:** optional but fill it in — in three weeks, `coupler` with no note is a mystery number.
- **"Insert into all Part Studios and Assemblies"** (checkbox at the bottom of the tab): leave it **checked** — this makes the variables resolvable in every tab without extra steps. (Yours already is.)

---

# Part 2 — Two Decisions Before Sketching

## 2.1 The layer stack (2D → 3D)

Your Linkage drawing is flat; real links collide. Each moving element gets a z-layer, stacking outward from the frame plate:

| Layer | z-range (mm from frame front face) | Contents |
|---|---|---|
| 0 | −4 → 0 | frame plate (extruded backward) |
| 1 | 0.5 → 4.5 | both gears (coplanar so they mesh) |
| 2 | 5 → 9 | rocker (E–G) |
| 3 | 9.5 → 13.5 | arm (D–G–H) with hook |

The crank pin D grows from the driven gear's front face up through layers 2–3. The G pin connects rocker→arm. The E post grows from the frame through layer 1's airspace (at x=90 it clears the driven gear rim, which ends at x≈66.7 — you'll verify with Interference Detection later).

## 2.2 Real gear teeth (this may move one point)

Pitch radii must sum to the center distance. With C–F = 40 mm and ratio 1:2:

- **Option A (used below, keep 40 mm):** module = 80/(z1+z2) = 80/60 = **1.333 mm**, teeth 20/40. A non-standard module is irrelevant for 3D-printed gears that only mesh with each other.
- **Option B (standard module 1):** center distance becomes 30 mm → put C at **(10, −50)**. Safe, because C is not part of the four-bar — only F, D, E, G, H define the walking motion. Choose B if you might ever buy off-the-shelf gears.

---

# Part 3 — The Skeleton Sketch, Click by Click (30 min)

Open **Part Studio 1**. This sketch is the single source of truth: every part will be positioned by projecting from it.

### 3.1 Start the sketch

1. Click **Sketch** (leftmost toolbar button). The dialog asks for a sketch plane.
2. In the feature list (left panel), click **Front plane**. The dialog accepts it.
3. Press **`n`** to view the plane flat-on. You'll see the Origin as a small dot at center.
4. In the feature list, double-click the new sketch's name and rename it **Skeleton**. (Rename religiously — a list of "Sketch 7, Sketch 8" is unnavigable.)

### 3.2 Place the three fixed points: C, F, E

*(Help: "Point (sketch tool)")*

1. In the sketch toolbar find the **Point** tool (a single-dot icon) and click it.
2. Click three times roughly where the points belong — all three **below and to the right of the Origin**, in a horizontal-ish row. Rough is fine; dimensions will fix them. Press **Esc** to drop the tool.
3. **Pin down C at (0, −50):**
   - Press **`d`**, click the first point, click the **Origin dot**, then move the mouse *sideways* until the preview shows the **vertical** distance; click to place, type `50`, Enter.
   - C also needs x = 0: click C, Ctrl+click the Origin, and apply the **Vertical** constraint from the toolbar (the icon with a vertical line). C is now directly below the Origin. The point should turn black.
4. **Pin down F:** press `d`, click C, click F, place the text *below* the pair to get the **horizontal** distance, type `40`, Enter. Then select F and C together and apply the **Horizontal** constraint so F sits level with C.
5. **Pin down E:** dimension F→E horizontally as `#ground` (type `#` and pick `ground` from the popup), and constrain E horizontal with F.

Checkpoint: three black points in a row at heights −50, at x = 0, 40, 90.

### 3.3 The crank line F→D

1. **Line tool** (or press its toolbar icon), click on **F** to start (watch for the coincident snap — the point highlights), drag up-and-right at a random angle, click to end, **Esc**.
2. Press `d`, click the line, place, type `#crank`, Enter.
3. **Do not dimension its angle.** This is the one deliberate freedom in the sketch — the crank angle. The line stays blue; that's correct. The endpoint of this line is **D**.

### 3.4 The straight arm D→G→H

1. Line tool: click on **D** (snap to the crank's endpoint), draw a long line up-and-right, click to end. This endpoint will be **H**. Esc.
2. Dimension the line `2*#coupler` (expressions are fine in any dimension box).
3. Place a **Point** roughly on the middle of that line. Select the point and the line, and apply the **Midpoint** constraint (icon looks like a point centered on a segment). That point is now **G**, permanently the arm's midpoint — collinearity and equal halves for free.

### 3.5 The rocker E→G

1. Line tool: click **E**, then click **G** (snap onto the midpoint). Esc.
2. Dimension it `#coupler`.
3. The moment this dimension lands, the sketch closes the four-bar loop: the arm and rocker snap into a legal pose. Everything should now be black **except** the crank line and whatever hangs off it.

### 3.6 Gear pitch circles (construction)

1. **Circle** tool: click center on **C**, drag out, click. Repeat centered on **F**. Esc.
2. Dimension the first circle's **diameter** `#module*#z1` and the second `#module*#z2`. (The dimension tool on a circle gives diameter by default.)
3. Select both circles (click, Ctrl+click) and press **`q`** — they turn dashed (construction). They should kiss exactly at one point between C and F. If they overlap or gap, a variable is wrong.

### 3.7 Test-drive the skeleton

Grab point **D** with the mouse and drag it in a circle around F. The whole skeleton — arm, G, H, rocker — must sweep through the walking gait, exactly like dragging in Linkage. If it locks up or flips, check the four length dimensions. When satisfied, click the **✓** to close the sketch.

> **Nitty-gritty worth knowing:** if while dragging the linkage "pops" to a mirrored pose, drag it back — sketches permit both solution branches. When you later assemble the real parts, mates will hold the correct branch.

### 3.8 Label the points (so you never keep a mental map)

Two complementary methods:

1. **Sketch text labels:** in the Skeleton sketch, use the **Text** tool ("A" icon in the sketch toolbar). Drag a small box *next to* each point (not on top of it — it makes the point hard to click), type the letter, ✓. Select each text and press **`q`** to make it construction, or the letter outlines will appear as extrudable profiles later. Label the moving points D, G, H slightly off to the side, since text doesn't follow the pose when you drag the crank.
2. **Named mate connectors (do this for C, F, E):** close the sketch, then use the **Mate connector** feature in the Part Studio toolbar (small coordinate-triad icon) → click a skeleton point → ✓ → rename the feature in the feature list: `C - motor axis`, `F - driven axis`, `E - rocker post`. These named connectors are selectable **in the Assembly's mate dialogs** (no more hovering cylindrical faces hoping the inferred connector lands right) and the Spur gear feature accepts them as its center input.

---

# Part 4 — Modeling the Parts (90 min)

All parts are built in this same Part Studio, positioned by **projecting skeleton points with the Use tool**. Pattern for every part: *offset plane → sketch → Use to project skeleton points → draw around them → dimension with variables → extrude with result "New".*

### 4.0 One-time skill: projecting a skeleton point

You'll do this constantly, so here it is once in full:

1. Inside any open sketch, click the **Use (Project)** tool in the sketch toolbar.
2. Click a point of the Skeleton sketch in the graphics area (hover until it highlights — if the sketch is hidden, hover its name in the feature list and click the eye icon to show it).
3. A fixed reference point appears in your current sketch, permanently linked to the skeleton. Circles you center on it are anchored to the mechanism. Esc to drop the tool.

### 4.1 Frame plate

**A. Start the sketch:**

1. With no other sketch open, click **Sketch**. In the dialog's "Sketch plane" field, select the plane via the **feature list**: click the **Front plane** entry near the top of the left panel (more reliable than clicking the translucent plane in graphics). Press **`n`**, then **`f`** if needed.
2. Rename immediately: double-click the new sketch's name in the feature list → `Frame profile`.
3. Prerequisite: the **Skeleton sketch must be visible** to project from — hover `Skeleton` in the feature list and click its **eye icon** if struck through.

**B. Project C, F, E:**

4. Activate **Use (Project/Convert)** in the sketch toolbar (hover icons for tooltips). Zoom in on point **C**, hover the point itself until it highlights, click. The tool stays active — click **F**, then **E**. Esc. The three projected points appear black instantly (projections are fixed by definition) and stay welded to the skeleton forever.
5. Pitfalls: clicking a skeleton **line** projects the whole line (Ctrl+Z, zoom in, retry); at F, don't grab the pitch **circle** by accident — the hover highlight shows what you're about to pick.

**C. Plate outline:**

6. **Corner rectangle** tool: click up-left of C (~10–15 mm margin), then down-right of E. Onshape auto-adds Horizontal/Vertical constraints to the edges — desirable here.
7. Either leave the outline blue (fine — nothing references the plate's edges) or, better, define it relative to the skeleton with four point-to-edge dimensions: `d` → projected point **C** → **left edge** → `15`; C → **top edge** → `15`; C → **bottom edge** → `15`; **E** → **right edge** → `15`. Point-to-line dimensions measure perpendicular distance, so each pins one edge — and the plate now grows automatically if `#ground` ever changes.
8. **Sketch fillet** tool: click each corner vertex, radius `6`. Note we dimensioned to *edges*, not corner points, precisely because filleting deletes the corner points — edge-referenced dimensions survive.
4. Holes: **Circle** centered on each projected point —
   - at **C** — the motor mount. Unlike F and E (fixed posts), C passes a **motor shaft**: the motor body sits *behind* the plate, its shaft pokes through, and the gear presses on in front. The plate therefore needs (1) a **Ø4 shaft-clearance hole** centered on C (N20 shaft is Ø3; if your motor has a raised locating boss around the shaft, size the hole to the boss instead), and (2) **two screw holes** matching the threaded holes in the motor's faceplate: for a typical N20, Ø1.8 (M1.6 clearance) at 9 mm either side of C — dimension each center 9 mm horizontally from C and apply a **Horizontal** constraint with C so all three holes sit in a row. **Measure your actual motor** (shaft Ø, screw spacing, thread size) before trusting these numbers. No motor yet? Sketch only the shaft hole and add the mounting holes later via right-click `Frame profile` → **Edit** — nothing downstream breaks.
   - at **F**: `#pinDia + 2*#clear` (dimension the circle's diameter with that expression).
   - at **E**: `#pinDia` (press-fit post).
5. **Extrude** (toolbar): select the **plate region** — click inside the outline but *away from every circle*; the region shades (see §0.4 on regions — unselected circles become the holes). Depth `#thk`, result **New**, and flip direction with the **two-arrow toggle next to Depth** so the plate grows **backward** (−z): the layer plan measures everything from the frame's front face at z = 0, so the plate's thickness must occupy −4→0, leaving z ≥ 0 free for gears and links. Don't tick the "Direction" checkbox (§0.4 trap). Rotate a few degrees, confirm the yellow preview grows away from the mechanism side, ✓.
6. In the **Parts list** (bottom of feature list), rename the new part `Frame`.

### 4.2 Motor gear (20T) — via the Spur Gear custom feature

*(Help: "Custom features")*

**A. Install the Spur gear feature (once per document):**

1. In the Part Studio toolbar, the **rightmost icon** opens the custom-features menu. Click it → **Add custom features**. A search dialog opens that searches Onshape's public documents.
2. Type **"Spur gear"**. Many community results appear. Use the **owner filter**: just under the search bar are small filter icons — click the **Onshape ("On.") logo** to show only documents owned by **Onshape Inc.**, which isolates the official sample. (If browsing unfiltered: prefer entries with a released **V-number** over ones marked "In progress" — those are someone's live working copy — and note that custom features carry distinctive colorful icons, while grey part thumbnails like "Spur gear (40 teeth)" are finished parts, not features.) Click the official one, close the dialog. The custom menu now contains **Spur gear** permanently for this document.

**B. Make the gear landing geometry:**

3. The gear feature centers itself on a **sketch vertex**, and it builds the gear on that vertex's sketch plane. Your skeleton points live on the Front plane, but the gears belong at z = 0.5 (layer 1). So make a dedicated plane and a tiny sketch:
   - **Plane** tool (toolbar) → type stays **Offset** → select **Front plane** in the feature list → distance `0.5`. Check the preview: the new plane must sit **in front** of Front plane (toward you in the `n` view, +z, the side the mechanism lives on). If it's behind, click the **flip arrow** in the dialog. ✓ Rename it `Gear plane`.
   - **Sketch on Gear plane**, rename `Gear centers`. **Use** → project skeleton points **C** and **F**. Close (✓). Two vertices, nothing else — this sketch exists purely to give the gear feature correctly-placed, correctly-planed center points.

**C. Generate the gear:**

4. Custom menu → **Spur gear**. In its dialog, top to bottom:
   - **"Sketch vertex or mate connector":** click the projected **C** vertex from `Gear centers` (zoom in; hover until the point highlights) — or click your named `C - motor axis` mate connector; the field accepts either.
   - **Depth** `#thk`. **Symmetric: unchecked** (symmetric grows both ways through the plane and wrecks the layer stack). Check the direction arrow next to Depth: preview must grow **+z**, away from the frame.
   - **Number of teeth** `#z1`. Input dropdown: **Module**, value `#module`.
   - **Built-in sanity check:** the **Pitch circle diameter** field recalculates as module × teeth — it must read **26.667 mm**, matching the skeleton's construction circle. Anything else means an input didn't take.
   - **Pressure angle** 20° and **Root fillet** 1/3: leave at defaults. **Profile offsets / Helical / Chamfer:** ignore. **Center bore: unchecked** — the D-shaft bore is cut manually next (a round bore can't make the D-flat anyway). ✓
5. In the **Parts list** (bottom of the left panel), rename the new part `Motor gear`.

**D. Cut the D-shaft bore:**

6. Start a sketch **on the gear's front face**: click **Sketch**, then click the flat front face of the gear itself (hover until the whole face highlights, then click). Press `n`. Sketching on faces is how you place features "on" a part — the sketch plane *is* that face.
7. **Use** → project **C** (projection works through space onto the sketch plane, so the Front-plane point lands correctly here too).
8. Draw a **circle** centered at projected C, dimension Ø `3` (N20 motors have a 3 mm D-shaft — verify yours with calipers). Draw a **line** cutting across the circle as a chord; dimension the **line-to-center distance** `1` mm (`d` → click line → click center point → place → 1). That's the D-flat: 3 mm shaft, 2 mm across the flat.
9. **Trim** tool: click the small arc *beyond* the chord (the sliver on the far side of the line) to delete it, and click any line overhang sticking outside the circle. What remains is a closed D-shaped profile.
10. **Extrude** → set the top dropdown to **Remove** → click inside the D region → End type **Through all**, direction into the gear. **Merge scope:** by default Remove cuts *every* part it passes through — click the Merge scope field and select only `Motor gear`, so nothing behind it gets accidentally drilled. ✓
11. If the printed fit is too tight later, reopen this sketch and add `#clear` to the diameter — history-based modeling means the change flows through.

### 4.3 Driven gear with integrated crank pin (40T)

1. **Spur gear** feature again: center = projected **F** vertex from `Gear centers` (or `F - driven axis` connector), teeth `#z2`, module `#module`, depth `#thk`, Symmetric off, same +z direction — pitch circle diameter must read **53.333 mm**. Rename the part `Driven gear`. Visual check: both gears at the same z-layer, teeth meshing at the midpoint between C and F.
2. **Bore at F:** sketch on its front face → Use-project **F** → circle Ø `#pinDia + 2*#clear` → Extrude **Remove**, Through all, Merge scope `Driven gear` only. (This gear spins freely on a fixed axle, hence the clearance.)
3. **Crank pin with integrated washer boss** — one sketch, two extrudes:
   - Sketch on the driven gear's **front face**, rename `Crank pin`. **Use** → project skeleton point **D**. Draw **two concentric circles** centered on it: Ø `#pinDia + 3` (boss) and Ø `#pinDia` (pin). The two circles subdivide the area into an inner disc and an outer ring. Close the sketch.
   - **Extrude 1 (the boss):** Extrude → **Add** → select **both** regions (inner disc *and* ring — the full Ø7 disc) → depth `2*#layerGap + #thk` (= 5 mm: it spans the gap, the rocker layer, and the next gap, so the arm rides on its top). **Merge scope:** `Driven gear`, so boss and gear fuse into one part. ✓
   - **Extrude 2 (the pin):** the sketch auto-hid after being consumed — hover `Crank pin` in the feature list and click the **eye** to show it again. Extrude → **Add** → select the **inner disc only** → depth `2*#layerGap + 2*#thk + 1` (= 10 mm: reaches through the arm layer plus 1 mm proud for a retaining cap). Merge scope `Driven gear`. ✓
   - Result: gear, washer-boss, and crank pin are **one printed part** — the literal 3D translation of Linkage's "connector D lives on the gear link." The pin's radial position (25 mm from F) came from the skeleton via projection; whatever pose the crank happened to be in is irrelevant, since the pin is *on* the gear.

### 4.4 Rocker (E–G)

1. **Plane** → Offset from **Front plane** → `5` (+z, flip if needed). Rename `Rocker plane`.
2. Sketch on it, rename `Rocker profile`. **Use** → project **E** and **G**.
3. The dog-bone outline, the easy way — Onshape has a **Slot** tool (stadium-shape icon in the sketch toolbar):
   - Activate **Slot** → click point **E** → click point **G** → the stadium rubber-bands; set/dimension its **width** to `#pinDia + 4` (= 8 mm). The slot's centerline endpoints snap coincident to the projected points, so the part is anchored to the skeleton.
   - *(Fallback if your toolbar lacks Slot: two Ø8 circles at E and G, two connecting lines, select each line + each circle → **Tangent** constraint × 4, then **Trim** the inward-facing arcs.)*
4. **Holes:** circles centered on E and on G (snap to the projected points), both Ø `#pinDia + 2*#clear`.
5. **Extrude** → **New** → select the slot region *between* outline and holes (click the shaded band; do **not** click inside the two hole circles) → depth `#thk`. ✓ Rename the part `Rocker`.
6. **G pin (grows from the rocker):** sketch on the **Rocker's front face** → Use-project **G** → circle Ø `#pinDia` → Extrude **Add**, Merge scope `Rocker`, depth `#layerGap + #thk + 1` (reaches through the arm plus 1 mm proud). The arm's G hole will ride on this pin.

> **Subtlety:** you projected G at the skeleton's *current pose*, so the rocker is modeled in that pose. That's fine — the E–G distance is locked to `#coupler` by the skeleton, so the part's length is pose-independent. Just avoid dragging the skeleton *between* projecting E and G within the same sketch.

### 4.5 Arm with hook (D–G–H)

1. **Plane** → Offset `9.5`. Rename `Arm plane`. Sketch on it, rename `Arm profile`. **Use** → project **D, G, H**.
2. **Body:** **Slot** tool from **D** to **H**, width `#pinDia + 4`. **Holes** at **D** and **G**: Ø `#pinDia + 2*#clear`. **No hole at H** — H gets the hook.
3. **The hook at H** — a C-shape whose crown sits over the rope:
   - Two **concentric circles** centered on projected **H**: inner Ø `#ropeDia + 1` (the rope channel, 0.5 mm slack all round) and outer = inner `+ 5` (2.5 mm wall).
   - Cut the opening: draw **two lines** from the inner circle to the outer circle, both pointing **downward and tilted slightly toward D** — they mark the edges of the mouth, roughly 40–50° apart (mouth gap a touch wider than `#ropeDia` so the hook drops onto the rope cleanly during re-grip). **Trim** the inner- and outer-circle arcs *between* those two lines. What remains is a C-shaped ring, opening down-and-back.
   - Why that orientation: during the flat grip phase the pull seats the rope deeper into the crown; during the return bulge, H rises ~23 mm and the downward mouth lets the hook lift straight off.
4. **Extrude** → **New** → depth `#thk` — and here's where **region selection** matters. The slot's end cap overlaps the hook rings, so the sketch is subdivided into many shaded regions. Click every region **except**: (a) anything inside the **inner rope circle** — that's the channel and must stay open, and (b) the **mouth wedge** between your two opening lines. Rotate slightly (right-drag) if you can't tell whether a sliver got selected; selected regions shade darker. ✓ Rename `Arm`.
5. Sanity check with **Measure**: click the hook's inner arc and the arm's D-hole edge — the 3D distance readout should be consistent with H being `2*#coupler` from D.

### 4.6 F axle and E post — integral with the frame

Simplest and strongest: grow them straight out of the frame so the frame prints as one piece, posts up.

1. Sketch on the **Frame's front face** (the face at z = 0, facing the mechanism), rename `Posts`. **Use** → project **F** and **E**.
2. Circles: at **F** Ø `#pinDia`; at **E** Ø `#pinDia`.
3. Extrude **Add**, Merge scope `Frame`:
   - You need **different heights**, so do two extrudes from this one sketch (unhide it for the second, as in §4.3): **F axle** depth `#layerGap + #thk + 1` (= 5.5 — through the gear plus proud); **E post** depth `2*#layerGap + 2*#thk + 1` (= 10 — up to and through the rocker).
4. Retention (keeps gears/links from walking off the pins): the low-tech printed solution is a **cap** — a Ø `#pinDia + 3` disc, 2 mm thick, with a Ø `#pinDia − 0.4` press hole — printed four times and pushed onto each proud pin end (F, E, D, G). Model one as a separate part (any sketch, two circles, Extrude New) and print with tight fit. Alternatives: a drop of glue on the pin tip, or drill-and-screw an M2 into the pin end.

**Housekeeping before Part 5:** hide all sketches and planes (feature list → hover → eye) so only the six parts show: Frame (with posts), Motor gear, Driven gear (with pin), Rocker (with G pin), Arm, Cap. Check the **Parts list** names — the Assembly inherits them.

---

# Part 5 — Assembly and Motion, Click by Click (40 min)

Open **Assembly 1**.

### 5.1 Insert and fix

1. Click **Insert** (top-left of the assembly toolbar). The dialog lists your document's tabs; `Part Studio 1` is selected — its parts appear as thumbnails.
2. Click each part once, then click in the graphics area to drop it. Scatter them; position doesn't matter yet. Close the dialog (✗).
3. Right-click the **Frame** → **Fix**. It gets a fix glyph and can no longer move. Exactly one fixed part per mechanism.

### 5.2 Revolute mates

*(Help: "Mate" and "Mate connectors")*

The concept: a **mate connector** is a snap-point with orientation that Onshape infers on the fly — hover any **cylindrical face or circular edge** and connector previews appear at its center. A **Revolute mate** joins two connectors allowing only rotation about their shared axis.

For each joint below:

1. Click **Mate** in the toolbar. In the dialog, set the type dropdown to **Revolute**.
2. Hover the **hole or pin of the first part** until the connector preview appears at the circular edge's center; click it.
3. Hover the matching feature on the second part; click. The parts snap together. If the part snaps in **facing the wrong way**, click the **flip (⟲) arrows** in the mate dialog to reverse primary-axis direction before accepting. ✓

Create these five, renaming each in the mate list as you go:

| Mate name | Part A feature | Part B feature |
|---|---|---|
| `Rev C motor` | Motor gear bore | Frame hole at C |
| `Rev F` | Driven gear bore | F axle (or Frame hole at F) |
| `Rev E` | Rocker hole at E | E post |
| `Rev D crank` | Arm hole at D | Crank pin on driven gear |
| `Rev G` | Arm hole at G | G pin on rocker |

**The moment of truth:** after `Rev G` closes the loop, the assembly has exactly one degree of freedom. **Drag the driven gear with the mouse.** The arm and rocker must sweep the walking gait exactly as in Linkage. If Onshape reports over-constraint or parts fight, one mate grabbed a wrong face — right-click the suspect mate → Edit and re-pick.

### 5.3 Gear relation

*(Help: "Relations — Gear")*

1. In the toolbar's **Relations** group, click **Gear relation**.
2. It asks for **two revolute mates**: click `Rev C motor` and `Rev F` (pick them in the mates list on the left — easier than in graphics).
3. Ratio: enter **20** and **40** in the two boxes (teeth counts; Onshape derives 1:2).
4. **Reverse direction checkbox:** external gears must counter-rotate. Drag the motor gear a little — if both gears turn the *same* way, edit the relation and toggle Reverse. ✓

Now dragging the motor gear drives the entire mechanism at correct ratio.

### 5.4 Animate

1. In the mates list, right-click **`Rev C motor`** → **Animate**.
2. Dialog: Start `0°`, End `720°` (two full crank cycles ⇒ four motor turns… actually 720° of *motor* = one crank cycle at 2:1 — set End `1440°` to watch two crank cycles), duration ~20 s, and press **play**. Loop if offered.
3. Watch it walk. Run your Linkage simulation side-by-side — the gait should be identical, now with real thicknesses.

### 5.5 Interference detection

*(Help: "Interference detection")*

1. Toolbar → **Interference detection** icon → select all parts → run. Colliding volumes highlight red.
2. It checks the **current pose**, so drag the motor gear ~45° and re-run, around a full crank cycle. Usual offenders: arm vs the top of the E post, hook vs the driven-gear rim at the bottom of the return, rocker vs crank-pin boss. Fix with `#layerGap` tweaks or longer pin steps — thanks to the variables, most fixes are one number.

### 5.6 Measuring (for verification)

Select any two entities (faces, edges, the hook's hole edge and a gear face…) — a **measurement panel** expands at bottom-right showing distance/angle. Use it to check: hook stroke ≈ 76 mm across the flat grip phase, and rope-to-gear-rim clearance at the hook's lowest pose.

---

# Part 6 — The Second Arm (when ready)

Your Linkage plan added a third gear, 1:1 with the driven gear, carrying a second crank 180° out of phase:

1. In the Part Studio: extend the skeleton with a point at (130, −50) and a mirrored crank/arm/rocker set (+90 mm in x for the E post → (180, −50)); reuse the Spur gear feature (teeth `#z2` for 1:1) — or mesh a small **idler gear** between the two 40T gears (two direction-flips = same direction, like a belt).
2. Duplicate rocker + arm (in the Insert dialog you can insert the same part twice).
3. In the assembly: mate the second train identically, **but before adding its gear relation, drag its crank 180° opposite the first**, then create the relation — the relation locks in whatever phase exists at creation time. Animate: alternating gait with grip overlap.

# Part 7 — Print and Build Notes

- Export: right-click a part (graphics or Parts list) → **Export** → STL, resolution Fine.
- Gears at module 1.33 print cleanly with a 0.4 mm nozzle / 0.2 mm layers; print lying flat, teeth up.
- **Print a fit coupon first:** one plate with a `#pinDia + 2*#clear` hole and one `#pinDia` pin; adjust `#clear` for your printer, regenerate, then print the real parts. This one variable controls every joint's feel.
- Lubricate printed gears with PTFE/silicone grease, never petroleum (attacks PLA).
- Drive: an N20 gearmotor around 30–60 RPM gives headroom; 15 RPM input → 7.5 RPM cranks ≈ 15 hand-over-hand steps/min.

---

**The mental-model shift in one line:** Linkage was *simulate first, dimensions emerge*; Onshape is *dimensions first (Variable Studio + skeleton), simulation confirms*. Keep the Linkage file — it remains the fastest place to test gait changes, and any change there is a two-minute variable edit here.
