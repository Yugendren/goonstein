# ISLAND.md — reference geography for the playable island

**What this is.** A research note on the *public physical layout* of Little St. James, US Virgin Islands,
assembled from satellite imagery, published aerial photography, mapping sites, nautical charts and
territorial permit reporting. It exists for one reason: M3 says "terrain from the public outline", and a
level designer needs a real coastline, a real scale and a real set of landmarks to distort.

**What this is not.** This document contains no account of the criminal case, no victims, no guests, no
allegations, and no living people by name. None of that is in the game and none of it is here. Every source
below was mined for shape, size, colour and elevation only.

**How this feeds the game.** Per DESIGN.md, the in-game island and every location on it are **fictional and
renamed**. This file is the survey; the map is a caricature drawn from it. Rule of thumb: keep the *outline*
and the *scale*, invent everything else. See "Turning this into a level" at the bottom.

---

## 1. Scale and shape

| Quantity | Value | Confidence |
|---|---|---|
| Area | 70–78 acres (28–32 ha; 0.28–0.32 km²) | Firm — sources give a range, not one figure |
| Coordinates | 18°18′00″N, 64°49′31″W | Firm |
| Long axis | **~950–1,050 m**, running roughly WNW–ESE | **Derived, not surveyed** — see note |
| Short axis | **~300–450 m** at the widest | **Derived** |
| Outline | Lopsided, tapering; broad at the west end, narrowing to a point in the south-west, ragged and pinched along the east | Loose — trace it off a map source, don't trust prose |
| Neighbour | Great St. James, ~165 acres, roughly triangular, immediately NW | Firm |
| Channel between them | "St. James Cut", charted depth 18 ft; width not published | Firm (depth), gap (width) |
| Distance to St. Thomas | Great St. James sits 0.4 km off St. Thomas; Little St. James lies beyond it. Boat trips run ~3 nm from Red Hook | Mixed — the 0.4 km is charted, the 3 nm is journalistic |

> **Note on the axis figures.** No published survey gives the island's length. The 950–1,050 m × 300–450 m
> figures are back-solved from the acreage: a 1,000 × 360 m ellipse is ~283,000 m², which is 70 acres. They
> are the right order of magnitude for laying out a play space and nothing more. If you want the true
> outline, trace the OpenStreetMap way (via the ontheworldmap page below) rather than trusting any prose
> description, this one included.

**Playable scale sanity check.** ~1,000 × 400 m is a good size for four players on foot with golf carts —
roughly 60–90 seconds to run end to end, which is about right for a 15-minute run with an extraction point
at one end.

---

## 2. Terrain and elevation

- **Sources disagree on the summit** and this is the single biggest gap in the public record:
  - PeakVisor lists the island at **25 m (82 ft)**.
  - topographic-map.com gives a maximum of **50 m (164 ft)**, an average of ~1 m, a minimum of 0 m.
  - Working figure: **one dominant hill somewhere in the 25–50 m band**, not a ridge line.
- The Virgin Islands are drowned mountain peaks; the island is **volcanic rock**, not coral or sand.
- Relief is concentrated: most of the island sits close to sea level, with **"rocky hillsides"** covered in
  dense low foliage rising to a single high shoulder. The built compound occupies the flatter western
  ground; the striped pavilion sits on high ground at the **south-west point**.
- No LIDAR DEM, no named USGS quad and no published contour map for the island turned up. If you want real
  contours you are generating them, not importing them.

**For the level:** one hill, one saddle, one flat western shelf, and cliffs on the exposed side. That is the
whole terrain brief. Do not build a mountain.

---

## 3. Coast, beaches and water

- **West end:** a small sand spit curling into a **quiet cove**, holding the island's only real beach. Small
  — reachable by boat or by paddling. This is the sheltered side.
- **North shore:** beach backed by a stand of **manchineel** (see vegetation — this matters, it's the
  poison-apple tree). Rocky either side.
- **East / north-east:** the **windward** side. Trade winds in the USVI blow from the east and north-east, so
  this shore takes the chop. *This is an inference from the regional wind pattern, not a statement any source
  makes about this specific island* — but it is how every cay in the chain works.
- **Offshore, west side:** a named dive site, **"The Ledges of Little St. James"** — coral ledges, undercuts
  and pinnacles at roughly **25 ft and 45 ft** depth.
- **Cliffs:** repeatedly described as present ("rocky hillsides", "bluffs") but **no published height
  figures**. Given a 25–50 m summit, anything above ~15 m of sea cliff is invention.

**For the level:** one landable beach (west cove, where the boat goes), one decorative beach (north), rock
and cliff everywhere else. The extraction point wants to be the sheltered west cove — which conveniently is
also the far end from the deep stuff at the south-west point.

---

## 4. Vegetation and fauna

- **Vegetation type:** arid **dry tropical scrub forest**, the standard USVI small-cay palette. Low, tough,
  grey-green, wind-sculpted. Not jungle. Not lawn, except where landscaped.
- **Documented species:**
  - **Manchineel** — the island is noted as having one of the best stands in the area, lining the north
    beach. Real hazard tree: toxic sap, poisonous fruit. Free comedy: a tree that hurts you if you shelter
    under it in rain.
  - **Cactus** — present and flowering; hummingbirds feed on it.
  - **Palms** — visible in aerials, presumably planted around the compound.
- No source breaks down native vs. landscaped planting beyond this. Everything else is your call.
- **Fauna:**
  - **Green iguanas** — *introduced*, roughly 25 individuals brought over from St. Thomas around 2002.
    They are not native. They were imported deliberately. (Keep this one. See LORE.md, Floor 6.)
  - **Corn snakes** — also introduced.
  - Herpetological survey literature exists for the island (Caribbean Herpetology / caribherp.org) but was
    not accessible.
  - No island-specific documentation found for seabirds, hermit crabs or feral livestock — all plausible for
    a USVI cay, none confirmed here.

---

## 5. Climate and weather

- Tropical: **75–85 °F (24–29 °C)** year round.
- **~45 in (1,140 mm) rainfall/year**, concentrated May–November, which is also hurricane season.
- Winter: stronger trades, less rain. Summer: more rain, lighter wind.
- **Hurricane exposure is documented for this island specifically.** In September 2017, Irma and Maria hit
  the USVI two weeks apart. The gold dome on the striped pavilion was blown off. Satellite imagery shows a
  structure present on 10 August 2017 and gone by 7 September 2017. A barge caused an estimated $160,473 in
  damage to the dock/pier that season. Across St. Thomas and St. John, Irma stripped most of the forest
  canopy and shoreline vegetation.

**For the level:** dusk-to-dawn is the run clock, so weather is mostly mood. But "the roof of the temple
blew off in a storm and nobody replaced it" is a free, true-shaped reason for the map's centrepiece to be
open to the sky.

---

## 6. Built structures

Compiled from aerial/drone photography, satellite image comparison over time, and USVI permit reporting.
Confidence is flagged per row. Positions are approximate and several are genuinely uncertain.

| # | Structure | Description | Where | Confidence |
|---|---|---|---|---|
| 1 | **Main house** | Stone/cream-walled colonnaded villa with a **turquoise-blue roof**, courtyard, cluster of outbuildings. Attributed to the architect behind several Aman resorts. Renovated after 2003. | Western end, on the flat shelf | Firm |
| 2 | **Guest cabanas** | **Four** single-storey cabanas, **blue roofs**, in a row beside the main house and pool | Western end | Firm |
| 3 | **Pool** | **Oval** pool with a detached **bathhouse / pool house** | Western end, beside the main house | Firm |
| 4 | **Stone cabana** | Separate stone-walled cabana used as a private residence | Near the main cluster | Loose |
| 5 | **The striped pavilion ("temple")** | Blue-and-white **striped, boxy, roughly cube-shaped** building, ~**30 ft tall**, ringed by a square pavilion with geometric paving. **Gilded dome** added between July 2013 and March 2014; March 2015 footage shows the dome plus **two large golden bird-like statues** on the roof. Dome lost to Hurricane Maria, 2017. | **South-west point**, on high ground | Firm |
| 6 | *(same, on paper)* | Permitted as an **1,800 sq ft "music room / pavilion"**: a **10-ft-tall octagonal** structure with porches on five sides, containing a grand piano, a living room and a bathroom. **What was built bore little resemblance to the approved drawings.** Reported interior: one large two-level room, wood floors, rugs, floor-to-ceiling bookcases, a small black grand piano, and a **ceiling mural of the celestial sphere with constellations and mythological figures**. | — | Firm (the permit); reported (the interior) |
| 7 | **Dock** | Wooden pier with a small **roofed dock house** at its end | Shoreline; exact side not confirmed by any source I could verify | Firm (exists); **uncertain (which shore)** |
| 8 | **Helipad** | Landing pad with **two small blue-roofed structures** beside it | Separate from the main cluster | Firm |
| 9 | **Staff / maids' quarters** | Servants' housing among the outbuildings | Near the main compound | Loose |
| 10 | **Maintenance One** | Building with **"sally port"-type doors** | Service area | Loose |
| 11 | **Maintenance Two** | White exterior, several sets of **double doors** | Service area | Loose |
| 12 | **Service yard** | The **eastern side** is given over to service buildings — generators, desalination, water pumps, workshops, storage, fuel, sewage. After ~2009 several were **partly buried into the landscape**, leaving only one or two exposed façades. | East end | Loose but consistent across sources |
| 13 | **Metal sheds** | Two, metal-clad | Service area | Loose |
| 14 | **Cinema** | A movie theatre is reported among the structures | Unlocated | Loose |
| 15 | **Sundial** | A large sundial-like ground feature visible from the air | Unlocated | Loose |
| 16 | **The bermed rectangle** | On the **east side**, a large rectangle that reads from satellite like a tennis court — but is ringed by a **high bermed wall of earth and stone**. Nobody has publicly established what it is. | East end | Reported; purpose unknown |
| 17 | **Paths** | Stone-paved walkways with **blue wave patterns** inlaid | Around the compound | Firm |
| 18 | **Roads** | Paved roads snaking through the hills, built for **electric golf carts** | Island-wide | Firm |

**Utilities.** A private **desalination** system dates to the 1997 infrastructure. In **2005** a combined
**power and fibre-optic cable** was laid from St. Thomas to the island, ending reliance on generators. No
water tower or cistern is specifically documented in the sources I found, despite being near-universal on
USVI cays — treat a cistern as plausible-but-unconfirmed.

**Construction history.** A 1996 magazine feature documents the pre-1998 state: **main house, guest house,
oval pool, three cabanas**. Everything else came later. After a 2023 sale to a private-equity buyer (~$60M
for both islands, with a stated plan for a 25-room resort), **several structures were demolished** — but
**sources conflict on whether the striped pavilion still stands**, with some reporting it razed and others
listing it among surviving buildings. As of 2026, no resort has opened and construction has been slow.

---

## 7. Great St. James, the neighbour

- **~165 acres (67 ha)**, roughly triangular, immediately NW across St. James Cut.
- **Largely undeveloped.** Reporting is explicit that no buildings were completed on it.
- **Christmas Cove**, on the western side, is a genuinely popular public snorkelling and mooring spot with
  **22 overnight mooring balls**. Boats go there. It is a normal Caribbean anchorage.
- **Ecology:** permit documentation lists **endangered corals** and the **Virgin Islands tree boa**.
- **Clearing work, publicly reported:** a stop-work order in December 2018 over unpermitted work (a beach-bar
  cabana and an expanded driveway); the territorial planning commissioner cited "excessive additional
  roadways carved out on the island, two jetties, and fill material placed into submerged lands", with
  potential fines over $1M. Clearing was photographed as late as mid-2019.

**For the level:** this is your skybox island and your "there's another one?" gag. Roads cut into a hillside
leading to nothing at all is a good visual joke and it's what's actually there.

---

## 8. Underground: what's claimed vs. what's established

**Marked as rumour throughout. Read this section as evidence status, not as facts about the world.**

**What is documented:**
- The word **"tunnel"** appears in released planning and maintenance correspondence connected to the
  property — emails and documents referencing a "tunnel building", flooring and upkeep of a subterranean
  space. This establishes that *the term was used in internal paperwork*.

**What is not established:**
- **No** confirmed layout, extent, depth or purpose. No engineering diagram, no aerial or LIDAR analysis, no
  official statement confirms a tunnel network. Summaries of the record state plainly that it has not been
  confirmed whether tunnels were actually built.
- Broader viral claims — island-to-mainland tunnels, vast underground complexes — have been **traced by
  fact-checkers to misread imagery, AI-generated pictures, and false geographic associations**. Those are
  debunked, and they are a separate and much larger claim than the narrow paperwork reference.

**Mundane explanations that fit the same evidence:**
- Cisterns. Every dry Caribbean cay is built over water storage.
- Septic and sewage tanks — the east-side service yard handles exactly this.
- Utility trenches for the 2005 power/fibre cable run.
- Drainage and storm culverts on a steep volcanic island in a 45-in rainfall band.
- **Partly buried service buildings** — confirmed by satellite comparison, and the single most likely thing a
  person is looking at when they say "there's something under there." A shed with earth up three sides
  genuinely looks like an entrance from above.

**How the game uses this:** it doesn't, factually. DESIGN.md's rule is that the current era is invented. The
descent, the tunnels, the whole hole is fiction the game makes up (see LORE.md). The *only* thing this
section contributes to the design is the joke shape: **the real answer to "what's under the island" is
almost certainly a septic tank, and that's funnier than a bunker.** Floor 2 of the descent should be a
cistern, and the players should be visibly disappointed.

---

## 9. ASCII map

Rough, schematic, and honest about what it doesn't know. **Positions of the dock, helipad, cinema and sundial
are not reliably established** — they are placed here for layout purposes, not documented.

```
                                    N
                                    ^
   0m      100     200     300     400     500     600     700     800     900    1000m
   |--------|-------|-------|-------|-------|-------|-------|-------|-------|-------|
                                                      1 char across = 25 m
                                                      1 row down    = 50 m

                    ...--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~--...
             ...--~~     N O R T H   S H O R E  (manchineel)    ~~--...
        ..-~~                                                          ~~-..
     .-~      [H]  o o o o          ~~~~~~~ road ~~~~~~~~~            [~~~]  ~-.
   .~    ###  ^^^  cabanas x4                                    [M1][M2]  berm  ~.
  (  cove  (O)  [ M A I N ]                                       [S] [S]  yard   )
   `~   beach   [ H O U S E ]  {sun}         .- hill 25-50m -.    east service    ~'
     `-~     [P]  courtyard                                             ~-'
        ``-~~    ==DOCK==                                        ..-~~''
             ```--~~                                     ..--~~'''
                     ``--~~                     ..--~~'''
                          `` [T] ~~~~~~~~~~~~~~''
                             SW point
          ~ reef: "The Ledges" ~
        (west/leeward, 25 & 45 ft)

  LEGEND
  ###  landable beach, west cove (sheltered)   -> boat lands here, extraction point
  (O)  oval pool                    [P]  pool house / bathhouse
  [H]  helipad + 2 blue-roof sheds  o o  the four blue-roof cabanas
  [T]  striped pavilion, gold dome, SW point, high ground   <- the "temple"
  [M1] Maintenance One (sally-port doors)  [M2] Maintenance Two (white, double doors)
  [S]  service buildings, part-buried into the slope (generators, desal, sewage)
  [~~] the bermed rectangle, east side, purpose unknown
  {sun} the sundial ground feature (location not established)
  ==   wooden dock + roofed dock house (shore not established)

  WIND: trades from E / NE  =>  east shore rough, west shore calm.
  HIGH POINT: one hill, somewhere between 25 m and 50 m. Not a ridge.
```

---

## 10. Confidence summary

| Claim | Status |
|---|---|
| ~70–78 acres, volcanic, one hill | **Firm** |
| Main house + 4 blue-roof cabanas + oval pool at the west end | **Firm** |
| Striped pavilion at the SW point, gold dome 2013/14–2017, permitted as a music pavilion | **Firm** |
| Dock, helipad, maintenance buildings, service yard, golf-cart roads all exist | **Firm** |
| Exact positions of dock / helipad / cinema / sundial | **Not established — invented above** |
| Island length and width in metres | **Derived from acreage, not surveyed** |
| Highest point | **Disputed, 25 m vs 50 m** |
| Cliff heights, beach lengths, channel width | **Not found** |
| Water tower / cistern | **Not documented; plausible** |
| Whether the striped pavilion still stands today | **Sources conflict** |
| Tunnels | **Word appears in paperwork; nothing else established. Treat as rumour.** |

---

## 11. Turning this into a level

DESIGN.md: *"The island is a play on a public satellite outline, with a made-up name."* So:

1. **Keep:** the outline, the ~1 km × 400 m scale, one hill, a sheltered west cove, a rough east shore, a
   compound on the flat western shelf, a folly on the south-west point, a service yard on the east, and
   golf-cart roads snaking between them. That's the whole geographic joke: *a very small island with an
   absurd amount of infrastructure on it.*
2. **Rename everything.** The island, the pavilion, the cove, the cut, the neighbour. All fictional.
3. **Exaggerate freely.** Real: 70 acres, one hill, four cabanas. Game: whatever reads at run speed.
4. **The good free jokes already in the geography:**
   - The iguanas were **imported**. Someone shipped 25 lizards to a private island on purpose.
   - The manchineel: a beach lined with trees that poison you if you stand under them in the rain.
   - The pavilion was permitted as a **10-ft octagonal music room** and built as a **30-ft striped cube with
     a gold dome and two giant metal birds on it**. The paperwork gag writes itself.
   - The dome **blew off in a hurricane** and nobody replaced it.
   - There are **roads carved into the neighbouring island that lead to nothing**, because the buildings were
     never built.
   - The east-side service buildings are **half-buried in the hillside**, which is the entire visual basis of
     "there's something under the island." It's the generator shed.
5. **Do not** import: the case, the people, the names, the dates as plot. None of it is in the game.

---

## 12. Sources

Geography, mapping and charts:
- https://en.wikipedia.org/wiki/Little_Saint_James,_U.S._Virgin_Islands
- https://en.wikipedia.org/wiki/Great_Saint_James
- https://ontheworldmap.com/virgin-islands-us/little-saint-james/ (OSM-derived outline map)
- https://en-us.topographic-map.com/map-zm8d3q/Little-Saint-James-Island/ (elevation viewer)
- https://peakvisor.com/poi/little-saint-james-island.html (elevation/coordinates)
- https://www.nauticalcharts.noaa.gov/publications/coast-pilot/files/cp5/CPB5_C14_WEB.pdf (NOAA Coast Pilot 5
  Ch. 14 — St. James Cut)
- https://reefsmartguides.com/product/ledges-of-little-saint-james/ (reef/dive site)
- https://www.wikidata.org/wiki/Q6651815

Structures, permits and construction:
- https://www.nbcnews.com/news/us-news/jeffrey-epstein-s-bizarre-blue-striped-building-private-island-raised-n1037511
  (permit records for the pavilion: 1,800 sq ft music room, octagonal 10-ft design vs. what was built)
- https://www.britannica.com/place/Jeffrey-Epsteins-Islands (overview; fetch returned 403, used via search)
- https://medium.com/@lafleurba/little-st-james-island-a-tour-cf552d746915 (satellite-comparison walkthrough
  of structures over time — service yard, part-buried buildings, bermed rectangle)
- https://www.axios.com/2025/12/03/epstein-island-images-photos-videos-democrats-owner (published imagery)
- https://www.newsweek.com/jeffrey-epstein-island-little-saint-james-images-2097148

Great St. James, clearing and hurricanes:
- https://stthomassource.com/content/2007/05/30/bulldozers-vs-great-st-james-put-your-money-island/
- https://www.virginislandsdailynews.com/news/construction-appears-to-continue-on-great-st-james/article_58f62584-cefe-5719-bef2-06d538ed2658.html
- https://www.virginislandsdailynews.com/news/epstein-s-barge-sued-for-damages-during-irma/article_dd3ff6ce-1a1e-5c94-af9a-b334a47f5f58.html
- https://www.fema.gov/disaster/historic/hurricane-irmamaria-us-virgin-islands
- https://stthomassource.com/content/2019/09/11/two-years-post-irma-recovery-of-plants-and-trees-continues/

Tunnel claims and their evidence status:
- https://www.wionews.com/trending/epstein-little-saint-james-island-home-had-tunnels-and-he-talked-endlessly-about-them-report-1770272770986
  (reports the term appearing in released documents — the narrow claim)
- https://factually.co/fact-checks/justice/epstein-island-tunnels-hatches-investigation-f4ae2f
  (fact-check of the broader viral claims; 403 on fetch, used via search snippet)
