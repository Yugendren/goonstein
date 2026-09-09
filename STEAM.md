# STEAM.md — can this game ship?

Research note, 2026-09-10. Question: would *Goonstein Island* as specced in DESIGN.md — satirical co-op
comedy on a fictional analogue of a billionaire's island, built from internet conspiracy memes, no real
people named, no crimes or victims depicted, no children anywhere — be allowed on Steam?

**Not legal advice.** Before launch, pay a media/IP solicitor for one hour on the character bible and the
store page. That hour is the cheapest line item in this document.

---

## Verdict

**Yes, with high confidence (~90%) that Steam accepts it.** Valve's published bar is "illegal, or straight
up trolling," and the DESIGN.md hard rules already clear it by a wide margin. The empirical proof is
blunt: a game literally titled **Epstein** has been selling on Steam since February 2024 (app 2786830,
"Mixed", 237 reviews, tagged Gore *and* Sexual Content, AI art, co-op survival on the island) and is live
as of today — verified directly. Its sequels and two *Legacy of Epstein* titles are also live. Those games
use the real name and darker framing than anything you have planned.

**The real risks are not Steam.** Ranked, they are: Australian classification on *implication*; UK
defamation over an archetype that maps onto one living person; and marketing/press blowback. Details below.

Confidence is high on Steam acceptance and moderate on everything downstream — Valve's "trolling" and
"unknown costs and risks" clauses are discretionary by design, and the payment-processor clause added in
July 2025 is a new, opaque veto that nobody outside Valve can predict.

---

## 1. What Valve actually forbids

From the Steamworks onboarding docs, "What you shouldn't publish on Steam" (fetched and verified verbatim,
<https://partner.steamgames.com/doc/gettingstarted/onboarding>):

- "Hate speech, i.e. speech that promotes hatred, violence or discrimination against groups of people based
  on ethnicity, religion, gender, age, disability or sexual orientation"
- "Nude or sexually explicit images of real people"
- "Adult content that isn't appropriately labeled and age-gated"
- "Libelous or defamatory statements"
- "Content you don't own or have adequate rights to"
- "Content that violates the laws of any jurisdiction in which it will be available"
- "Content that is patently offensive or intended to shock or disgust viewers"
- "Content that exploits children in any way"
- (plus malware, phishing, unrelated video, non-interactive 360 VR, crypto/NFT, ad-based business models)
- "Content that may violate the rules and standards set forth by Steam's payment processors and related
  card networks and banks, or internet network providers" — **added July 2025**, see §5

Only four of these are live questions for this project: defamation, exploiting children, "patently
offensive… intended to shock or disgust", and the payment-processor clause. The design rules answer the
first two. The third is subjective and is why the store page matters more than the game.

The governing policy statement is Valve's June 2018 blog post *Who Gets To Be On The Steam Store?*:
"We've decided that the right approach is to allow everything onto the Steam Store, except for things that
we decide are illegal, or straight up trolling." Valve conceded it is vague and said they assess the
developer as much as the game: "We investigate who this developer is, what they've done in the past."
<https://steamcommunity.com/games/593110/announcements/detail/1666776116200553082>

**Review process.** $100 per app (recouped at $1,000 revenue). Store-presence review and build review are
each ~3–5 business days; plan for 7+ to absorb one round of revisions. A content survey is mandatory:
general content (drives regional ratings), mature content self-declaration, and generative-AI declaration.
Mature descriptors are General Mature Content / Some Nudity or Sexual Content / Frequent Nudity or Sexual
Content / Adult Only Sexual Content / Frequent Violence or Gore. The last two force an 18+ affirmation.
Valve's reviewers compare your answers against the build and the store page — under-declaring is the
classic own goal. <https://partner.steamgames.com/doc/gettingstarted/contentsurvey>

---

## 2. Precedent — things that shipped and stayed

- **Epstein** (app 2786830, Feb 2024), **Epstein 2** (3029230), **Legacy of Epstein: Bad Omen** (1829810)
  and **Foul Dreamer** (2911160) — all live. The first uses parody names for real living figures
  ("Danold Tramp", "Clintin", "Ball Gates") and survived the July 2025 purge untouched, which press noted
  as an inconsistency. <https://www.thegamer.com/steam-valve-epstein-game/>
- **itch.io** has a live, indexed `tag-epstein` browse page with 16 titles (verified today), including the
  *Five Nights at Epstein's* family that went viral in US schools. <https://itch.io/games/tag-epstein>
- **Real-name political satire ships routinely**: *The Political Machine 2024* (real politicians, real
  likenesses), *Mr. President!* (a "Ronald Rump" analogue, 82% positive), and a long tail of
  Trump/Biden/Putin parody titles. None removed.
- **Postal 2 / Postal 4**, **Hatred**, **Party Hard** — sustained "edgy" content, all still selling.
  Hatred was pulled from Greenlight in 2014 and personally reinstated by Gabe Newell within 24 hours.
  <https://www.pcgamer.com/hatred-reinstated-on-steam-greenlight/>
- **Conspiracy** (QAnon satire), **ILLUMINATI** (app 1816070), **The Illuminati Simulator** (1429070) —
  conspiracy-lore games are an established, unmolested niche.

## 3. Precedent — things Valve removed or refused

Every documented removal falls into one of three buckets, and none of them is "satirised a real event."

| Case | Year | Valve's actual reason |
|---|---|---|
| **Active Shooter** | 2018 | Developer conduct, *not* the school-shooting premise: "a troll, with a history of customer abuse, publishing copyrighted material, and user review manipulation" |
| **Rape Day** | 2019 | Discretionary: "poses unknown costs and risks" — a judgement call, not an illegality finding |
| **AIDS Simulator, ISIS Simulator, Suicide Simulator** | 2018 | The "trolling" carve-out — game-shaped objects, not games |
| **Super Seducer 3** | 2021 | "Steam does not ship sexually explicit images of real people" (1 and 2 stayed up) |
| **~2018 anime/VN purge** | 2018 | Explicit sexual content, after an NCOSE pressure campaign |
| **Digital Homicide (whole catalogue)** | 2016 | Developer sued ~100 Steam users; conduct removal |
| **2025 wave, 100+ titles** | 2025 | Payment-processor clause; titles containing "incest", "rape", "slave" |

**The key negative finding:** across all of this, there is **no documented case of Valve permanently
removing a game solely for satirising, depicting, or naming a real person** absent an independent trigger
(explicit sexual imagery of a real person, developer misconduct, or a copyright claim). That is an
absence-of-adverse-precedent finding, not an affirmative clearance — but it is the right shape of absence.

Two cases outside Valve's history that get miscited: **Six Days in Fallujah** was dropped by *Konami*, not
a platform; **Super Columbine Massacre RPG!** was cut from *Slamdance*, and never touched Steam.

---

## 4. Regions

- **Australia is the sharpest edge.** "Refused Classification" bans sale outright. Any sexualisation of a
  child-like character is an absolute, effectively unappealable RC trigger, and the Board reads
  **implication**, not just depiction — dialogue, item descriptions and environmental storytelling all
  count. Given the subject matter, the "no children anywhere" rule is doing genuine legal work. Other RC
  triggers: sexual violence (*Hotline Miami 2*, still banned) and drug use tied to progression rewards
  (*We Happy Few* and *Disco Elysium*, both RC'd then overturned on appeal). *Saints Row IV* shipped after
  removing one weapon — precedent that a surgical cut usually suffices.
  <https://en.wikipedia.org/wiki/List_of_banned_video_games_in_Australia>
- **Germany.** Age ratings are mandatory (JuSchG, since the 2021 reform) and USK ratings must display on
  the storefront. §86a StGB bans unconstitutional-organisation symbols, but since August 2018 games get the
  same art/social-adequacy carve-out film always had, assessed case by case — critical or satirical
  depiction is now defensible. Only relevant if any incidental prop echoes real extremist iconography;
  don't put one in. <https://usk.de/en/home/obligations-for-content-providers/>
- **Brazil** (ClassInd) and **South Korea** (GRAC) both legally require ratings for games sold there,
  Steam included; Korea's enforcement is complaint-driven rather than universal.
- **UK.** PEGI 12/16/18 are legally enforceable at point of sale. Steam now enforces Online Safety Act age
  verification for UK users viewing mature-flagged pages.
- Steam does **not** use IARC — it has its own content survey. An Australian RC is a regional block, not a
  global delist: Valve kept selling *Hotline Miami 2* and *Disco Elysium* everywhere else.

---

## 5. Payment processors — the actual new gatekeeper

In July 2025 the Australian group Collective Shout lobbied Visa, Mastercard and PayPal over games depicting
rape, incest and child sexual abuse on Steam and itch.io. Rather than the card networks issuing a rule, they
leaned on the platforms' processing relationships, and both capitulated pre-emptively. Valve added the
payment-processor clause and delisted 100+ titles; itch.io mass-deindexed **all** NSFW-tagged content
overnight because it could not manually vet 2M+ pages. <https://itch.io/updates/update-on-nsfw-content>
<https://www.abc.net.au/news/2025-08-03/adult-video-games-removed-from-steam-after-campaign/105597886>

Every reported takedown targeted **sexual** abuse content. No source reports non-sexual satire being caught,
and the Epstein-titled games on both platforms rode through it untouched. The practical rule this hands you:
**never acquire a sexual-content tag.** The moment this game has one, it enters the only category anyone is
actually enforcing, and the subject matter makes it a far more attractive campaign target than the average
visual novel. This is the single highest-leverage design constraint in the document.

---

## 6. Defamation and likeness

- **Epstein himself is dead (2019). You cannot defame the dead in the US.** No estate or survivor claim for
  reputational harm exists in nearly every state. This is settled, and it is load-bearing for the premise.
  <https://splc.org/2019/10/can-you-libel-a-dead-person/>
- **Living associates are the exposure, and not naming them is not a defence.** The test is "of and
  concerning": would a reasonable person who knows the real individual identify the character as them? UK
  courts add **jigsaw identification** — role + era + relationship network + one signature incident can add
  up to identification even when no single detail does.
- **This is a live problem with DESIGN.md as written.** The archetype list is "The Prince, The Financier,
  The Scientist, The Guy Who Only Flew There Once." *The Prince* on a private-island-scandal map maps onto
  exactly one living person for any UK audience, and *The Guy Who Only Flew There Once* is a quotation of a
  specific real public denial. These are archetypes in name but jigsaw identifiers in function. Fix by
  compositing: change the rank (a duke's cousin, a deposed minor royal of an invented country), scramble the
  era and the signature beats, and make sure no character's biography rhymes with one real person's.
- **UK law is far more claimant-friendly than US law.** No actual-malice requirement. The Defamation Act
  2013 added a serious-harm threshold (s.1) and honest-opinion (s.3) and public-interest (s.4) defences,
  which help, but a UK claimant *can* sue over a fictional character, and defending even a losing claim is
  ruinous. <https://www.legislation.gov.uk/ukpga/2013/26>
- **Likeness.** *Keller v. EA* and *Hart v. EA* lost for EA because the depictions were realistic and
  faithful; *Brown v. EA* won under the *Rogers v. Grimaldi* test. The deciding factor is fidelity, not
  inspiration. The planned art direction — hand-drawn, wobbly, Roblox/Peak-style bodies — is a real,
  structural mitigation: it pushes every character toward caricature and away from likeness.
- **Title/trademark.** *Rogers v. Grimaldi* protects real-world references inside an expressive work unless
  the title is artistically irrelevant or explicitly misleading. *Jack Daniel's v. VIP* (2023) narrowed this
  only where you use someone's mark as your **own** source identifier. Keeping "Epstein" out of the title
  sidesteps the carve-out entirely. Note separately that **"Goonstein" is a poor working title**: it is
  transparently the real surname plus a syllable, and "goon" now carries a specific sexual connotation
  online. The alt, *Rescue Something*, is strictly safer and funnier.
- **Disclaimers do not work as law.** "All characters fictional" has not defeated an identification claim
  since the 1933 Rasputin case. Ship one anyway — it costs nothing and frames intent — but the actual
  defence is composite character design.

---

## 7. Other platforms, for context

Permissiveness: **itch.io ≳ Steam > GOG > Epic > consoles.**

- **itch.io** — most permissive for satire, but the bluntest instrument if you ever trip the NSFW tag.
- **Epic** — hand-curated by humans. Guidelines prohibit "real-world torture, death, or mutilation" and
  content that "demean[s], dehumanize[s], marginalize[s]" people. Defensible here, but expect friction and
  possible change requests. <https://legal.epicgames.com/epicgames/content-guidelines>
- **GOG** — curated, policy sparse, genuinely untested here. Coin flip; ask them directly.
- **Consoles** — least predictable; cert teams reject on reputational grounds beyond the rating (*Manhunt 2*
  was refused by both Sony and Nintendo). Not a first-launch target. Ship PC first.
- **YouTube/Twitch** — expect demonetisation of your own trailer and reluctance from streamers, purely on
  keyword classification, regardless of what is on screen. Do not plan on streamers as primary discovery.

---

## 8. Risks, ranked

1. **Australia RC over implication of minors.** Highest severity, and the only one that is unappealable in
   practice. Mitigation: the existing hard rule, extended to text, props, audio and environment art.
2. **UK defamation over an identifiable archetype.** Currently *live* — see The Prince and The Guy Who Only
   Flew There Once above. Cheap to fix now, expensive later.
3. **Acquiring a sexual-content tag.** Would move the game into the one enforced category, with an
   unusually motivated set of campaigners. Fully within your control.
4. **Store page framing.** A page screenshottable as "go to the real island" is a rejection and backlash
   risk independent of the build. Valve's discretionary levers ("trolling", "unknown costs and risks") are
   applied to optics, not code.
5. **Press cycle / review bombing.** Not a policy risk, but a real one. Nothing in DESIGN.md's premise is
   defensible in a headline that omits the punchline, so the punchline must be in the first screenshot.
6. **Residual Valve discretion.** Unpredictable by construction. Low, but never zero.
7. **Germany §86a, right of publicity, trademark.** Low, already covered by existing decisions.

---

## 9. Pre-submission checklist

**Content**
- [ ] **Purge the inherited child assets from the fork.** `assets/characters/child.txt`,
      `assets/scenes/lantern_child.txt`, and the `npc Child` line at `assets/levels/lantern.txt:281` all
      came over from *hollow*. A shipped build of *this* game whose asset manifest contains "child" is an
      unforced error of the highest order. Delete them and grep the whole tree for `child`, `kid`, `minor`,
      `boy`, `girl`, `teen` before every submission.
- [ ] No character model, silhouette, voice, prop, poster, text string or audio implies a person under 18.
- [ ] No sexual content of any kind. No nudity, no innuendo about the real case, no "sexual content" tag.
- [ ] Every archetype recomposited so no character maps to one living person (start with The Prince).
- [ ] Nothing presented as factual is post-1970, per the existing rule. Audit LORE.md against it.
- [ ] Island and every location fictionally named; outline distorted from the survey, not traced 1:1.
- [ ] If the coastline is traced from OpenStreetMap, honour ODbL attribution — or hand-draw it and moot it.
- [ ] Victims: absent, unreferenced, un-joked. Verify by reading every string in the build, not by memory.

**Store and submission**
- [ ] Title is not "Goonstein". Pick something that does not contain a mangled real surname.
- [ ] Store page leads with the satire in the first line and the first screenshot: four idiots, a boat, no
      kids, memes all the way down. The joke must survive being screenshotted without context.
- [ ] Content survey filled in honestly and generously: Violence, General Mature Content. Nothing sexual.
- [ ] Declare AI-generated content if any is used.
- [ ] In-game and store-page disclaimer: fictional island, fictional persons, satire.
- [ ] Capsule art PG-13 per Steam's asset rules; no real likeness, no real name, no review scores or awards.
- [ ] Budget 7+ business days for review; do not schedule marketing beats against a 3-day assumption.
- [ ] Submit a build that plays, with a real vertical slice — "game-shaped object" is Valve's trolling test.
- [ ] Publish under a clean developer account with no prior Valve conduct history.
- [ ] One hour of media/IP counsel on the character bible and store copy before you hit publish.

**Sources.** URLs inline above. Primary: Steamworks onboarding rules and content survey; Valve's 2018
*Who Gets To Be On The Steam Store?*; live Steam pages for apps 2786830 / 3029230 / 1829810 and itch.io's
tag-epstein page (both fetched and verified 2026-09-10); itch.io's July 2025 NSFW update; UK Defamation
Act 2013; Epic's content guidelines. Secondary: ABC, PC Gamer, CNN, Vice, TheGamer.
