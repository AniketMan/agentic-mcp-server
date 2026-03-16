# LEVEL SEQUENCE MASTER REFERENCE

## HOW TO USE THIS REFERENCE
For EVERY Level Sequence listed below, the Worker MUST:
1. Verify the LS asset exists via `list_sequences` or Content Browser
2. Verify ALL listed actors are spawned and visible BEFORE the LS plays
3. Verify the trigger condition is wired in the Level Blueprint
4. Verify the LS is bound to the correct actors via `execute_python`
5. Verify the transition/next action fires when the LS completes

If ANY of these checks fail, the scene is NOT complete.

---

## SCENE 00: Tutorial
No named Level Sequences. All logic is event-driven (markers, gaze, grip, trigger).

---

## SCENE 01: Home - Larger Than Life

### LS_1_1 -- Heather Child Entrance
- **Script Beat:** Heather (Child) bursts into living room, twirling and dancing, runs to Susan
- **VO:** SUSAN: "This is my home-- And THAT little whirlwind is my daughter, Heather Heyer..."
- **Actors That MUST Be Spawned:** `BP_HeatherChild`, `BP_VoiceSource`, `BP_AmbienceSound` (fridge hum)
- **Trigger:** Level Start (BeginPlay)
- **On Complete:** Enable `UObservableComponent` on `BP_HeatherChild` for gaze interaction
- **Transitions To:** Gaze sequence (player gazes at Heather -> NS_JoyfulAura)

### LS_1_2 -- Hug Animation
- **Script Beat:** Heather hugs Susan's leg, heartbeat haptic plays
- **VO:** SUSAN: "These moments--these little, everyday moments--are what stay with you."
- **Actors That MUST Be Spawned:** `BP_HeatherChild` (with `UGrabbableComponent` proxy)
- **Trigger:** Player grips `BP_HeatherChild` (OnInteractionStart from UGrabbableComponent)
- **On Complete:** Enable `BP_TeleportPoint` near kitchen table, Heather runs to kitchen
- **Transitions To:** LS_1_3

### LS_1_3 -- Heather Runs to Kitchen
- **Script Beat:** Heather releases grip, skips into kitchen, settles at table
- **VO:** [Heather GIGGLES]
- **Actors That MUST Be Spawned:** `BP_HeatherChild`
- **Trigger:** After LS_1_2 completes
- **On Complete:** Begin cross-fade
- **Transitions To:** LS_1_4

### LS_1_4 -- Cross-Fade Child to PreTeen
- **Script Beat:** Heather (Child) transforms into Heather (Pre-Teen) at kitchen table
- **VO:** None
- **Actors That MUST Be Spawned:** `BP_HeatherChild` (fading out), `BP_HeatherPreTeen2` (fading in)
- **Trigger:** After LS_1_3 completes
- **On Complete:** Scene 02 begins (continuous)
- **Transitions To:** Scene 02 / LS_2_1

### LS_HugLoop -- Extended Hug Loop
- **Script Beat:** If player holds grip longer, hug animation loops
- **VO:** None
- **Actors That MUST Be Spawned:** `BP_HeatherChild`
- **Trigger:** Player continues holding grip after LS_1_2
- **On Complete:** When player releases grip -> LS_1_3
- **Transitions To:** LS_1_3

---

## SCENE 02: Standing Up For Others

### LS_2_1 -- Heather at Table Drawing
- **Script Beat:** Heather (Pre-Teen) sits at kitchen table drawing with colored pencils
- **VO:** SUSAN: "Whenever the house grew quiet, I always had to check on Heather. Silence usually meant she was up to something."
- **Actors That MUST Be Spawned:** `BP_HeatherPreTeen2`, `BP_VoiceSource`, `BP_LocationMarker` (table), `BP_TeleportPoint` (table)
- **Trigger:** After Scene 01 completes / Level Start for Scene 02
- **On Complete:** Enable `BP_LocationMarker` for player to walk to table
- **Transitions To:** Player walks to marker -> illustration interaction

### LS_2_2R -- Illustration Animates (Classroom Vignette)
- **Script Beat:** Grabbing the paper activates animated illustration: children in classroom, teacher scolds boy, Heather stands up, goes to detention with him
- **VO:** SUSAN: "I didn't know at the time, but she got in trouble for this..." [full detention story]
- **Actors That MUST Be Spawned:** `BP_Illustration` [makeTempBP], `BP_VoiceSource`
- **Trigger:** Player grabs `BP_Illustration` (OnInteractionStart from UGrabbableComponent)
- **On Complete:** Paper auto-returns to table, enable `BP_TeleportPoint` (door), highlight `BP_Door`
- **Transitions To:** LS_2_3

### LS_2_2R1 -- Illustration Variant 1
- **Script Beat:** Alternate illustration animation
- **Actors That MUST Be Spawned:** Same as LS_2_2R
- **Trigger:** Variant selection (if implemented)
- **Note:** May be unused variant. Verify with Content Browser if asset exists.

### LS_2_2R2 -- Illustration Variant 2
- **Script Beat:** Alternate illustration animation
- **Actors That MUST Be Spawned:** Same as LS_2_2R
- **Trigger:** Variant selection (if implemented)
- **Note:** May be unused variant. Verify with Content Browser if asset exists.

### LS_2_3 -- Heather Runs Out Door
- **Script Beat:** Heather grins, runs out the front door. [Heather GIGGLES]
- **VO:** SUSAN: "Sometimes doing the right thing isn't easy, but that was her way."
- **Actors That MUST Be Spawned:** `BP_HeatherPreTeen2`, `BP_Door` (highlighted)
- **Trigger:** After LS_2_2R completes
- **On Complete:** Reactivate teleportation, highlight door knob
- **Transitions To:** Scene 03 (player grabs door)

---

## SCENE 03: Rescuers

### LS_3_1 -- Friends Enter, Sit at Table
- **Script Beat:** Door opens, Heather (Teen) sweeps in with Friend 1 and Friend 2, they sit at kitchen table
- **VO:** SUSAN: "She always had this way of bringing people in--friends, strays, you name it..."
- **Actors That MUST Be Spawned:** `BP_Heather_Teen`, `BP_FriendMale`, `BP_FriendFemale`, `BP_Door`
- **Trigger:** Player grabs door knob (OnInteractionStart on BP_Door UGrabbableComponent)
- **On Complete:** Friends seated, enable fridge interaction
- **Transitions To:** Fridge interaction

### LS_3_1_v2 -- Friends Enter (v2)
- **Note:** Variant. Check Content Browser for existence.

### LS_3_2 -- Heather Gets Glasses from Cabinet
- **Script Beat:** Heather stands on tippy toes, reaches into cabinet for glasses
- **VO:** SUSAN: "We didn't have much, but Heather made sure everyone felt like they belonged."
- **Actors That MUST Be Spawned:** `BP_Heather_Teen`
- **Trigger:** After fridge opens and pitcher is grabbed
- **On Complete:** Glasses placed on table, enable pour interaction
- **Transitions To:** Pour sequence

### LS_3_2_v2 -- Cabinet (v2)
- **Note:** Variant. Check Content Browser for existence.

### LS_3_3_v2 -- Cheers Animation
- **Script Beat:** Heather and friends raise glasses for cheers, take a drink
- **VO:** [Glass CLINK over LAUGHTER]
- **Actors That MUST Be Spawned:** `BP_Heather_Teen`, `BP_FriendMale`, `BP_FriendFemale`, `BP_Glass` x3
- **Trigger:** All 3 glasses filled (glassesFilledCount == 3)
- **On Complete:** Friends retreat to hallway
- **Transitions To:** LS_3_7

### LS_3_5 -- Heather Retrieves Glasses
- **Script Beat:** Heather retrieves glasses and carries them to table
- **VO:** SUSAN: "Sometimes I worried she was taking on too much--"
- **Actors That MUST Be Spawned:** `BP_Heather_Teen`
- **Trigger:** After friends sit (during fridge sequence)
- **On Complete:** Glasses on table
- **Transitions To:** Pour interaction

### LS_3_5_V2 -- Glasses (v2)
- **Note:** Variant. Check Content Browser for existence.

### LS_3_6 -- Pour Interaction
- **Script Beat:** Water filling glasses
- **VO:** [Water FILLING SFX]
- **Actors That MUST Be Spawned:** `BP_PourablePitcher`, `BP_Glass` x3
- **Trigger:** During pour (visual feedback)
- **On Complete:** Continue pour sequence
- **Transitions To:** LS_3_3_v2 (when all filled)

### LS_3_7 -- Cheers, Friends Retreat
- **Script Beat:** Friends retreat into hallway with glasses in hand
- **VO:** SUSAN: "Even in the hardest times, we believed that it wasn't about what you have-- It's about what you're willing to share."
- **Actors That MUST Be Spawned:** `BP_Heather_Teen`, `BP_FriendMale`, `BP_FriendFemale`
- **Trigger:** After cheers animation
- **On Complete:** Enable Scene 04 progression
- **Transitions To:** Scene 04

---

## SCENE 04: Stepping Into Adulthood

### LS_4_1 -- Phone Notification Setup
- **Script Beat:** Living room, late afternoon. Phone on side table, text DING
- **VO:** SUSAN: "Now don't get me wrong. Heather wasn't anything special..."
- **Actors That MUST Be Spawned:** `BP_PhoneInteraction`, `BP_SfxSound` (ding), `BP_VoiceSource`
- **Trigger:** After friends leave (Scene 03 complete)
- **On Complete:** Phone screen lights up, enable grab
- **Transitions To:** LS_4_2

### LS_4_2 -- Text Conversation Active
- **Script Beat:** Messages appear in 3D space: "Hey Mom, what's your SSN?" / "Why do you need that?"
- **VO:** SUSAN: "She'd moved out, gotten her own place..."
- **Actors That MUST Be Spawned:** `BP_PhoneInteraction`, `BP_SimpleWorldWidget` (3D text)
- **Trigger:** Player grabs phone (OnInteractionStart)
- **On Complete:** Continue text sequence
- **Transitions To:** LS_4_3

### LS_4_3 -- Text Conversation Continuation
- **Script Beat:** "I'm adding you as my beneficiary" / "Well I'd rather have you than money" / "Mom!" / "I love you" / "Love you more!"
- **VO:** SUSAN: "My heart swelled, but like any parent..."
- **Actors That MUST Be Spawned:** `BP_PhoneInteraction`, `BP_SimpleWorldWidget`
- **Trigger:** Player presses trigger to advance messages
- **On Complete:** All messages shown
- **Transitions To:** LS_4_4

### LS_4_4 -- Phone Down, Door Transition
- **Script Beat:** Phone deactivates, front door highlighted
- **VO:** SUSAN: "It's hard to let go, but seeing her become her own person-- That was a gift."
- **Actors That MUST Be Spawned:** `BP_PhoneInteraction`, `BP_Door` (front)
- **Trigger:** Player drops phone (OnInteractionEnd)
- **On Complete:** Highlight front door
- **Transitions To:** Scene 05 (player grabs door -> SwitchToSceneLatent)

---

## SCENE 05: Dinner Together

### LS_5_1 -- Establish Scene, Heather Waiting
- **Script Beat:** Restaurant, Heather (Adult) sits at center table waiting excitedly
- **VO:** SUSAN: "Heather and I would meet for dinner just about every other week..."
- **Actors That MUST Be Spawned:** `BP_Heather_Adult`, `BP_AmbienceSound` (restaurant ambience), `BP_TeleportPoint` (table), `BP_LocationMarker` (chair), `BP_VoiceSource`
- **Trigger:** Level Start (BeginPlay)
- **On Complete:** Player walks to marker
- **Transitions To:** LS_5_1_intro_loop or LS_5_2

### LS_5_1_intro_loop -- Ambient Waiting Loop
- **Script Beat:** Heather waiting animation loops until player arrives
- **VO:** None
- **Actors That MUST Be Spawned:** `BP_Heather_Adult`
- **Trigger:** During LS_5_1
- **On Complete:** Player arrives at marker
- **Transitions To:** LS_5_2

### LS_5_2 -- Heather Talking Energetically
- **Script Beat:** Heather talks a mile a minute, hands lively, eyes bright
- **VO:** SUSAN: "She was talking a mile a minute--"
- **Actors That MUST Be Spawned:** `BP_Heather_Adult`
- **Trigger:** Player sits down (marker overlap, teleportation disabled)
- **On Complete:** Heather sips drink
- **Transitions To:** LS_5_3

### LS_5_3 -- Heather Sips, Reaches Out
- **Script Beat:** Heather takes sip of whisky and coke, reaches hand out
- **VO:** SUSAN: "I wanted to tell her everything would be fine, but I couldn't lie."
- **Actors That MUST Be Spawned:** `BP_Heather_Adult`, `BP_HandPlacement`
- **Trigger:** After LS_5_2 completes
- **On Complete:** Enable hand hold interaction
- **Transitions To:** LS_5_3_grip

### LS_5_3_grip -- Hand Hold Interaction
- **Script Beat:** Player places hand palm-up, Heather places her hand in Susan's
- **VO:** None (haptic: heartbeat fast -> steady)
- **Actors That MUST Be Spawned:** `BP_Heather_Adult`, `BP_HandPlacement`
- **Trigger:** Player aligns hand with placement prompt (OnInteractionStart on UGrabbableComponent)
- **On Complete:** Heartbeat slows, environment fades to black
- **Transitions To:** LS_5_4

### LS_5_4 -- Intimate Moment, Fade
- **Script Beat:** Only Susan and Heather at table in darkness, Heather smiles with watery eyes. Narrator speaks.
- **VO:** SUSAN: "Holding on to her whisky laugh, infectious smile..." + NARRATOR: "Hold this moment... but know it's only part of the story."
- **Actors That MUST Be Spawned:** `BP_Heather_Adult`, `BP_VoiceSource`
- **Trigger:** After hand hold sequence
- **On Complete:** Fade to black, end Chapter 1
- **Transitions To:** Scene 06 (SwitchToSceneLatent)

---

## SCENE 06: The Rally in Charlottesville

### LS_6_1 -- Echo Chamber Forms
- **Script Beat:** Selected shape multiplies, pours out of computer, forms prism enclosure around player. Social media posts on walls.
- **VO:** NARRATOR: "In these echo chambers, illusions become truth--fueled by fear, falsehoods, and the need to belong." + "Weeks before the rally, white supremacist groups organized online..."
- **Actors That MUST Be Spawned:** `BP_PCrotate`, `BP_ShapeSelector` [makeTempBP], `BP_EchoChamber` [makeTempBP]
- **Trigger:** Player selects shape (OnInteractionStart on UActivatableComponent)
- **On Complete:** Opening appears in echo chamber wall, path to Station 2
- **Transitions To:** Station 2 (scale)

### LS_6_2 -- Matches Ignite Domino
- **Script Beat:** Weight placed on lit match side, matches ignite one by one, tiki torch silhouettes appear
- **VO:** NARRATOR: "On August 11, hundreds marched by torchlight, turning a lone spark of anger into a blazing show of intimidation." + "Fueled by the power of ritual..."
- **Actors That MUST Be Spawned:** `BP_Balance`, `BP_Weight` [makeTempBP], `BP_ScaleDropZone` [makeTempBP], `BP_TorchMarcher` [makeTempBP] (array)
- **Trigger:** Weight placed on correct side of scale (OnWeightPlaced broadcast)
- **On Complete:** Gap opens in torch circle, path to Station 3
- **Transitions To:** Station 3 (cradle)

### LS_6_3 -- Cradle Collision + First Confrontation
- **Script Beat:** First pull+release: slight collision, two giant silhouettes appear (protester + counter-protester)
- **VO:** NARRATOR: "In these moments, a single nudge can tip strangers toward confrontation..."
- **Actors That MUST Be Spawned:** `BP_nCrate`, `BP_CradleSphere` [makeTempBP]
- **Trigger:** First pull + release of cradle sphere (OnCradleCollision broadcast, count=1)
- **On Complete:** Cradle resets with 3 red spheres
- **Transitions To:** LS_6_4 (second pull)

### LS_6_4 -- Chaos Erupts
- **Script Beat:** Second pull: stronger collision, more silhouettes, both groups lunge, shoving, debris, chaos
- **VO:** NARRATOR: "By midday, momentum built with every angry voice. Shouts became shoves..."
- **Actors That MUST Be Spawned:** `BP_nCrate`, `BP_CradleSphere` [makeTempBP], silhouette actors
- **Trigger:** Second pull + release (OnCradleCollision broadcast, count=2)
- **On Complete:** Wind tunnel exit opens, path to Station 4
- **Transitions To:** Station 4 (torn sign)

### LS_6_5 -- Car Falls, Blackout
- **Script Beat:** Player grabs torn sign, spotlights flicker OFF, car chain releases, car falls, blackout before impact. Then phone rings.
- **VO:** NARRATOR: "At 1:42 PM, a car drove into a group of counter-protesters, injuring 35 people and killing one-- Heather Heyer."
- **Actors That MUST Be Spawned:** `BP_cardbaordTorn`, `BP_car`, `BP_PhoneInteraction` (spawns after blackout), `BP_Door_Hospital` [makeTempBP] (spawns after phone call)
- **Trigger:** Player grabs torn sign (OnInteractionStart on UGrabbableComponent)
- **On Complete:** Phone call plays, hospital door spawns
- **Transitions To:** Scene 07 (player walks toward door -> SwitchToSceneLatent)

---

## SCENE 07: The Hospital

### LS_7_1 -- Lobby Establish
- **Script Beat:** Sterile lobby, reception desk, two officers standing to right
- **VO:** SUSAN: "I got the call while I was out with a friend. No one knew where Heather was. I was frantic, calling every hospital."
- **Actors That MUST Be Spawned:** `BP_Recepcionist`, `BP_Officer1`, `BP_Officer2`, `BP_LocationMarker` (desk), `BP_VoiceSource`
- **Trigger:** Level Start (BeginPlay)
- **On Complete:** Enable location marker
- **Transitions To:** LS_7_2

### LS_7_2 -- Reception Desk
- **Script Beat:** Receptionist slides number 20 card toward Susan
- **VO:** SUSAN: "When I finally got there, they handed me a number--"
- **Actors That MUST Be Spawned:** `BP_Recepcionist`, `BP_NumberCard` [makeTempBP]
- **Trigger:** Player overlaps desk marker (OnInteractionStart on UTriggerBoxComponent)
- **On Complete:** Teleportation disabled, card slides to player
- **Transitions To:** LS_7_3

### LS_7_3 -- Officers Approach
- **Script Beat:** Susan grabs card, officers approach and take her arms, haptic on arms
- **VO:** SUSAN: "A NUMBER-- Where is my daughter?" + "That's all I needed-- to know."
- **Actors That MUST Be Spawned:** `BP_NumberCard`, `BP_Officer1`, `BP_Officer2`
- **Trigger:** Player grabs number card (OnInteractionStart on UGrabbableComponent)
- **On Complete:** Officers take Susan's arms
- **Transitions To:** LS_7_4

### LS_7_4 -- Hallway Walk (Guided 3DoF)
- **Script Beat:** Officers guide Susan down hallway, tunneled vignette, walls closing in, heartbeat intensifies
- **VO:** OFFICER: "Ms. Bro, please come with us." + SUSAN: "They took me by the arms." + "I kept hoping she was just hurt, just unconscious. My baby girl--"
- **Actors That MUST Be Spawned:** `BP_Officer1`, `BP_Officer2`, `BP_HospitalSequence`, `BP_NumberCard` (still in hand)
- **Trigger:** After LS_7_3 (officers take arms)
- **On Complete:** Reach meeting room door
- **Transitions To:** LS_7_5

### LS_7_5 -- Enter Meeting Room
- **Script Beat:** Officers let go, block doorway. Detective stands waiting inside.
- **VO:** None (ambient tension)
- **Actors That MUST Be Spawned:** `BP_Officer1`, `BP_Officer2`, `BP_Detective`
- **Trigger:** After hallway walk completes
- **On Complete:** Susan enters room
- **Transitions To:** LS_7_6

### LS_7_6 -- Detective Delivers News
- **Script Beat:** Detective: "Ma'am, your daughter has been pronounced." Susan frozen. All sound vanishes. Ringing. Susan wails. Vision blurs. Fade to black.
- **VO:** DETECTIVE: "Ma'am, your daughter has been pronounced." + SUSAN: "It didn't make sense. Everything just-- stopped." + [UNGODLY WAIL] + "I didn't get to see her-- Couldn't hold her-- Couldn't even say goodbye--"
- **Actors That MUST Be Spawned:** `BP_Detective`, `BP_VoiceSource`, `BP_NumberCard` (still in hand)
- **Trigger:** Susan enters meeting room
- **On Complete:** Fade to black, end Chapter 2
- **Transitions To:** Scene 08

---

## SCENE 08: Turning Grief Into Action

### LS_8_1 -- Memory Matching Setup
- **Script Beat:** Kitchen table with scattered notes, documents, sympathy cards. Four photos of Heather at different ages. Familiar objects around home.
- **VO:** SUSAN: "In those first days, the whole world wanted to know who Heather was. Reporters camped outside, day and night..."
- **Actors That MUST Be Spawned:** `BP_MemoryMatching`, Photos x4, `BP_Teapot_Grabbable` [makeTempBP], `BP_Illustration_Grabbable` [makeTempBP], `BP_PourablePitcher`, `BP_PhoneInteraction`, `BP_VoiceSource`, `BP_AmbienceSound` (rain + reporters)
- **Trigger:** Level Start (BeginPlay)
- **On Complete:** Enable all object interactions
- **Transitions To:** Object matching (any order)

### LS_8_2 -- Teapot Match -> Child Heather
- **Script Beat:** Player matches teapot to childhood photo. Heather (Child) appears at table smiling.
- **VO:** SUSAN: "Heather was always full of life, imagination pouring from her like magic." + [Heather Child GIGGLING]
- **Actors That MUST Be Spawned:** `BP_Teapot_Grabbable`, `BP_PhotoDropZone` (childhood), `BP_HeatherChild` (spawns on match)
- **Trigger:** Correct match: teapot dropped on childhood photo zone
- **On Complete:** matchCount++, Heather Child visible at table
- **Transitions To:** Next match (any order)

### LS_8_3 -- Illustration Match -> PreTeen
- **Script Beat:** Player matches illustration to pre-teen photo. Heather (Pre-Teen) appears, joins Child.
- **VO:** SUSAN: "Heather's fairness defined her early on." + [Heather Pre-Teen HUMMING]
- **Actors That MUST Be Spawned:** `BP_Illustration_Grabbable`, `BP_PhotoDropZone` (preteen), `BP_HeatherPreTeen2` (spawns on match)
- **Trigger:** Correct match: illustration dropped on preteen photo zone
- **On Complete:** matchCount++, Heather PreTeen visible at table
- **Transitions To:** Next match (any order)

### LS_8_4 -- Pitcher Match -> Teen
- **Script Beat:** Player matches sealed pitcher to teen photo. Heather (Teen) appears, joins hands with others.
- **VO:** SUSAN: "Heather saw the good in everyone, sharing whatever we had." + [Heather Teen LAUGHTER]
- **Actors That MUST Be Spawned:** `BP_PourablePitcher`, `BP_PhotoDropZone` (teen), `BP_Heather_Teen` (spawns on match)
- **Trigger:** Correct match: pitcher dropped on teen photo zone
- **On Complete:** matchCount++, Heather Teen visible, holds hands with others
- **Transitions To:** Next match (any order)

### LS_8_5 -- Phone Match -> Adult + Circle Forms
- **Script Beat:** Player matches phone to adult photo. Heather (Adult) appears, completing circle. All Heathers hold hands, extend toward Susan.
- **VO:** SUSAN: "Heather became someone I deeply admired--responsible, thoughtful..." + "Heather showed me how a single person, living authentically, can inspire countless others."
- **Actors That MUST Be Spawned:** `BP_PhoneInteraction`, `BP_PhotoDropZone` (adult), `BP_Heather_Adult` (spawns on match), ALL previous Heathers
- **Trigger:** Correct match: phone dropped on adult photo zone (matchCount == 4)
- **On Complete:** All Heathers hold hands in circle, extend hands to Susan
- **Transitions To:** Circle of Unity (hand hold)

### LS_8_Final -- Circle of Unity Connection
- **Script Beat:** Player grabs both Heather hands, completing the circle. Transition to Scene 09.
- **VO:** SUSAN: "Now, it's my turn to continue what she started--"
- **Actors That MUST Be Spawned:** `BP_HandPlacement` (left), `BP_HandPlacement` (right), ALL four Heathers
- **Trigger:** Player grips both hands (OnInteractionStart on both UGrabbableComponents)
- **On Complete:** Circle connection animation, fade
- **Transitions To:** Scene 09

---

## SCENE 09: Legacy in Bloom

### LS_9_1 -- Flowers Begin Blooming
- **Script Beat:** Home fades away, luminous space, purple flowers sprout and bloom everywhere
- **VO:** SUSAN: "After losing Heather, I didn't know how to move forward. I knew I couldn't replace her, but I could honor her. And slowly, with support from so many others, I learned to transform pain into purpose."
- **Actors That MUST Be Spawned:** `FlowerOpen_clones`, `LS_flowers_animation`, `BP_VoiceSource`, `BP_AmbienceSound` (ethereal)
- **Trigger:** Level Start (BeginPlay)
- **On Complete:** Flowers bloomed
- **Transitions To:** LS_9_2

### LS_9_2 -- Foundation Imagery
- **Script Beat:** Images emerge: Heather Heyer Foundation scholarship recipients
- **VO:** SUSAN: "We created the Heather Heyer Foundation, supporting passionate individuals dedicated to making positive change. Scholarships helped students pursuing justice, equality, and compassion."
- **Actors That MUST Be Spawned:** `BP_Image_Player`
- **Trigger:** After LS_9_1 completes
- **On Complete:** Images displayed
- **Transitions To:** LS_9_3

### LS_9_3 -- Youth Programs
- **Script Beat:** Images: youth activism groups, Rose Bowl Parade, Smithsonian, "Heyer Voices" program
- **VO:** SUSAN: "Through partnerships and community programs, we empowered youth to raise their voices against hate, inspired by Heather's bravery and compassion."
- **Actors That MUST Be Spawned:** `BP_Image_Player`
- **Trigger:** After LS_9_2 completes
- **On Complete:** Images displayed
- **Transitions To:** LS_9_4

### LS_9_4 -- NO HATE Act Signing
- **Script Beat:** Animated signatures form into legislative documents
- **VO:** SUSAN: "Together, we advocated for change-- real, lasting change. Laws that improve how hate crimes are reported and responded to, ensuring no one's suffering goes unseen."
- **Actors That MUST Be Spawned:** `BP_Image_Player`
- **Trigger:** After LS_9_3 completes
- **On Complete:** Documents displayed
- **Transitions To:** LS_9_5

### LS_9_5 -- Media Appearances
- **Script Beat:** Media snippets: MTV VMAs, talk shows, community gatherings
- **VO:** SUSAN: "The world wanted to understand who Heather was. Through conversations, through education, and through community-- we made sure they saw the heart of who she was. Her legacy lives on in every person inspired to act."
- **Actors That MUST Be Spawned:** `BP_Video_Player`
- **Trigger:** After LS_9_4 completes
- **On Complete:** Media displayed
- **Transitions To:** LS_9_6

### LS_9_6 -- Sunrise Finale
- **Script Beat:** Flowers and images swirl into mosaic, sun rises to zenith, golden warmth. Fade to white. End experience.
- **VO:** SUSAN: "Heather showed me that every small action matters, every voice counts. And when we choose love over hate, compassion over fear, we plant something powerful-- something lasting. Ordinary people can do extraordinary things. These are the seeds Heather planted, the legacy she left behind. A gentle reminder that HOPE-- still exists."
- **Actors That MUST Be Spawned:** Sun (Directional Light), `BP_PlayerCameraManager`, all flower/image actors
- **Trigger:** After LS_9_5 completes
- **On Complete:** Fade to white. END OF EXPERIENCE.
- **Transitions To:** Credits / End

---

## TOTAL: 56 Level Sequences across 9 scenes (Scene 00 has none)

## VERIFICATION CHECKLIST
For EACH Level Sequence above:
- [ ] Asset exists in Content Browser (call `list_sequences` or check CB dump)
- [ ] All listed actors are spawned BEFORE the LS trigger fires
- [ ] The trigger condition is wired in the Level Blueprint (listener node -> play sequence)
- [ ] The LS is bound to correct actors (check via `execute_python`)
- [ ] The "On Complete" action is wired (OnFinished delegate -> next action)
- [ ] The transition to the next LS/scene is wired
- [ ] The VO lines match the script (correct SoundWave asset bound)
- [ ] After wiring, RE-READ THIS REFERENCE to confirm nothing was missed
