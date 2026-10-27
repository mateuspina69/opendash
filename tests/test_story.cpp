// Story / cutscene port (game/story + game/script_engine) vs. the game, tick by tick
// below). Each golden holds the game state at the call boundaries of the field mode FUN_005413a0, its frame body
// FUN_00542280 and the window pass FUN_005578a0. Per tick the test loads the state at
// the field-mode entry and runs the ported FUN_005413a0 with hooks; at every boundary it compares the ported step's
// result and reloads the game's state before the next step, so one step's mismatch does not cascade:
//   3 event runner + room scripts (ST39's inline case starts script 5)    5 the script runner (the cutscene script:
//   state functions, step functions, camera script + script actors)       8 the player input / state dispatch
//   9 pool 20 (the cutscene actors)   11 pool a0 (their effects)   13 actor collision   14 the player update
//   16 the camera (script mode 1)    17 pool a0 late     21 the end of the mode handler     24 the windows
// The pools / steps not ported (pool e0 / 40 / 10 / 60, FUN_0050f350, FUN_0058cc10, the draw pass, FUN_00415560 ..)
// are reloaded from the golden. Voice clips and music are MP3 streams the game waits on: both sides use the tick model
//   test_story <dat dir> <dash2.exe> <tests/data> [max reported failures] [name filters, comma-separated]
// Recipes:
//   story_ST39_open0.bin  --frames 120   (new game: stage load, field mode, ST39 case -> script 5 state 0, the
//                         narration voice 0x12, step 0)
//
//   story_ST1E_allout.bin  --from D:/story_spike/ck_1e_base.bin --driver st1e --frames 300
//   story_ST1E_blast.bin   --from D:/story_spike/ck_ok2/ck_8700.bin --driver st1e --frames 255
//   story_ST1E_timeout.bin --from D:/story_spike/ck_to/ck_7600.bin --frames 400
//   story_ST1E_fail.bin    --from D:/story_spike/ck_to/ck_8400.bin --frames 560
//   story_ST1E_data.bin    --from D:/story_spike/ck_ok4/ck_11400.bin --driver st1e --keep-hp --frames 400
//   story_ST1E_success.bin --from D:/story2/ck_s/ck_11700.bin --driver st1e --keep-hp --frames 460
//   (the success chain, no input, checkpoints D:/story2/ck_s; --from ck_N.bin [--skip S] --frames F as N/S/F:)
//   story_ST3A_s6a 12000/0/400  s6b 12900/100/180  s6c 14100/100/300  s6d 14700/0/200  s6e 15300/250/450
//   s6f 15900/100/400  s6g 16200/100/300  s6h 16800/150/100  s6i 17100/250/300  s6j 17400/100/300
//   s6k 17700/100/340  story_ST08_s55 18000/100/300  story_ST04_s4c 18300/60/340
//   (the attract intro, scripts 3 / 4 in ST02 / ST03 (content/intro/st02_attract_script / st03_attract_script), 2026-10-09; checkpoints ckS of
//   story_AT_00000.bin --stage 2 --poke 0xa6d18e 3 --poke 0xa6d196 1 --sound-init --frames 500
//   story_AT_<NNNNN>.bin --from ckS/ck_<N>.bin --frames 500 for N = 500, 1000, ... (the whole of scripts 3 / 4)
//   story_AT_skip.bin --from ckS/ck_2000.bin --script "20 -; 2 start; 400 -" --frames 57 (start: block flag 0x10,
//   script 3 state 2 -> *0xa7a000 = 2, the task switch back to the title)
//
//   story_ST08_free.bin   --from D:/story3/ck/ck_15600.bin --frames 300
//   story_ST08_follow.bin --from D:/story3/ck/ck_15600.bin --frames 400
//                         --script "80 up; 40 up+left; 80 up; 60 -; 60 down; 80 -"
//   story_ST09_roll.bin   --from D:/story3/ck/ck_15600.bin --driver story3 --frames 300
//   --from D:/story3/ck3/ck_N.bin --driver story3 --frames F as N/F: story_ST09_shop 15700/400
//   story_ST0A_s4d_a 16000/400  s4d_b 16400/400  story_ST09_s4e 16700/400  story_ST08_shed 17000/400
//   story_ST081_s4f_a 17400/400  s4f_b 17700/400  story_ST0D_field 18000/300  story_ST0F_ruin 18200/400
//   (the Flutter, story.md section 11; save 02 checkpoints in D:/story3)
//   story_ST04_flutter_exit.bin --from D:/story3/s02_st04.bin --teleport 250 0 -384 --yaw 3072 --frames 300
//                         --script "1 up; 1 up+circle" x 30 "; 240 -"
//   story_ST04_roll_menu.bin --from D:/story3/s02_roll.bin --frames 200
//                         --script "20 -; 1 circle; 60 -; 1 down; 10 -; 1 circle; 107 -"
//   story_ST01_travel.bin --from D:/story3/s02_map.bin --frames 140 --script "40 up; 30 -; 1 circle; 69 -"
//
//   --from D:/story4/ck/ck_N.bin --driver story4 --frames F as N/F: story_ST0F_s50a 18400/400  s50b 18800/400
//   sub5 19200/300  sub10 19500/400  s51b 19900/300  s52 20200/300  fight 20500/400  s53 20900/300  s54 21200/350
//   exit 21500/300  story_ST47_s12 21800/450; story_ST0F_sub5_idle --from ck_19200 --frames 400;
//   sub5_turret --from ck_19200 --teleport 4600 0 -3600 --frames 400; sub8 --from ck_19200 --teleport -3220 0 4096
//   --yaw 1024 --script "1 up+circle; 1 up" x 40 "; 220 -" --frames 300
//   (encounter layouts, actors.md 4b; new game from save 00 with a forced RNG seed at the stage setup:)
//   story_ST4F_layA / B / C.bin --save DASH2-00.DAT --stage 4F --sub 0 --seed 0xa6cecc1b / 0xcd613e30 / 0x7ed4d57b
//   --frames 150; story_ST4E_1_layA / B --stage 4E --sub 1 --seed 0x414c343c / 0xd8f16adf; story_ST50_0_layA / C
//   --stage 50 --sub 0 --seed 0xd8f16adf / 0x414c343c; story_ST4F_layC_pace.bin = layC with --frames 600 (the
//   0x4a2a50 sub-4 sentry's whole pacing cycle, behaviours 0 / 1 / 2 / 3 / 5 / 6)
//
//   story_ST1D_crystal.bin --from ck_crystal.bin --driver boss --frames 300
//   --from ckb/ck_N.bin --driver boss --frames F as N/F: story_ST1D_s28b 400/300  s28c 700/300  s28d 1000/200
//   story_ST1D_fight 1100/300  story_ST1D_death 1400/106
//   story_ST1D_passive.bin --from ckb/ck_1100.bin --driver boss-passive --keep-hp --frames 400
//   ST04 sub 0 after 60 ticks, Roll talked to with circle every 10 ticks from (176, 0, 140) yaw 0 for 352 ticks = s1map/ck_418,
//   then "44 left; 2 -; 1 circle" -> s1st10 (every 100), s1chain from s1st10/ck_end_1068 (every 100))
//   story_ST49_s18a.bin --from s1map/ck_end_418.bin --frames 300 --script "44 left; 2 -; 1 circle; 300 -" (the pick of
//                         ST10 on map 1 -> ST49 sub 2, script 0x18 state 0, step 0)
//   story_ST49_s18b / c / d / e: --from s1st10/ck_700, s1st10/ck_1000, s1chain/ck_1300 (300 ticks), s1chain/ck_1600
//                         (260: the end, state 2, request 2 -> ST10)
// the gate walk
//   --tp -3840 -768 3072 2048 "60 up; 300 -" with checkpoints every 250 -> s2tron/ck_N, the shooting run "1 square; 3 -"
//   repeated from s2tron/ck_4750 every 125 -> s2fight/ck_N)
//   story_ST25_s1b_00.bin --from s2/ck_end_63.bin --teleport -3840 -768 3072 --yaw 2048 --script "60 up; 300 -" --frames 250
//   story_ST25_s1b_NN.bin --from s2tron/ck_(NN*250).bin --frames 250 (NN = 01..18: the whole script 0x1b)
//   story_ST25_boss_N.bin --from s2tron/ck_N.bin --frames 250 (N = 4750..6750 step 250: the fight, the player passive)
//   story_ST25_fight_N.bin --from s2fight/ck_N.bin --frames 250 --script "1 square; 3 -" x 70 (N = 4875..6125 step 250)
//   tank's hit points poked to 2 and shot) -> ck (every 500) / ck2 (from ck_4500, every 10); the tank dies at 4770)
//   story_ST25_s1c_00.bin --from ck2/ck_4760.bin --frames 340 (flag 0x589 -> script 0x1c state 0, steps 0 / 1)
//   story_ST25_s1c_N.bin --from ck/ck_N.bin --frames 500 (N = 5000..7500 step 500: script 0x1c to its end, the
//   arrival in ST25 sub 0 at story 4 and its handler 0x581dc0)
//   (the Manda ruin gate, story 4, story.md 13.6;
//   p/a0 (+30 ticks); "exit 1" -> b1 (ST24/1); tp 3584 -640 940 0 + "1 up; 1 up+circle" x 45, 300 more -> b3 (ST24/2);
//   tp 0 -64 30 0 + the same x 45, 600 ticks every 100 -> c0_N; from c0 1300 ticks every 250 -> c1_N (ST12/0 at 10868))
//   story_ST24_sub0s4.bin --from p/a0.bin --teleport -256 -1024 -4052 --yaw 0 --script "90 up" --frames 250
//   story_ST24_gate.bin --from p/b3.bin --teleport 0 -64 30 --yaw 0 --script "1 up; 1 up+circle" x 45 --frames 560
//   story_ST24_s14a --from c0_9300 --frames 500   s14b --from c1_9750 --frames 500   s14c --from c1_10250 --frames 595
//   story_ST12_arrive.bin --from c1_10750 --frames 250 (script 0x14's request -> ST12 sub 0, handler 0x57e380)
//   (script 0x13 in ST1C, story.md section 15: ck_crystal (as section 14: save 00 + StoryRun(0x1d, 1, sound_init), 80 ticks, teleport, "10 -; 10 up; 5 -"), then
//   --from ck/ck_N.bin --driver boss --frames F as N/F: story_ST1C_s13a 1500/300  s13b 2000/450  s13c 3500/650
//   s13d 4000/300  s13e 4500/700  s13f 5000/600  s13g 7500/350  s13h 8500/650  s13i 9500/400  s13j 11500/450
//   s13k 13500/500  s13l 14500/750 (the end: request 2 -> ST3E sub 3, story 2)
//   story_ST3E_s64.bin: ST3E sub 0 (ck3e/w1 = ck/ck_15500 + the door of sub 3: teleport 640 0 -144 yaw 3072, circle)
//   --from w1.bin --teleport 0 0 1020 --yaw 2048 --frames 1050 --script "20 -; 1 circle; 30 -; 1 circle; 30 -;
//   1 circle; 10 -; 1 circle; 200 -" + "; 1 circle; 9 -" x 80 (the door to sub 6, script 0x64, request -> sub 1)
//   story_ST3E_s64b.bin --from t2.bin --frames 450 --script "1 circle; 9 -" x 40 (t2 = s64's run continued: the
//   sub-1 talk closed with cross, the door of sub 1 at (-512, 0, -880) yaw 0: ST3E sub 0, script 0x64 again with
//   flag 0x5a0 = messages 0x7c / 0x7d, flags 0xe0 / 0xe1)
//   (ST12 -> ST13 sub 1, story.md 13.7: go_step from c1 (ST12/0): tp -2212 -272 1024 1024 + "1 up; 1 up+circle" x 45
//   400 ticks -> d1 (ST12/1); tp -3584 -192 -5950 0 + "30 up" + "1 circle; 9 -" x 40, 500 -> d2 (the door actor -> ST13/0);
//   60 -> e0; tp 2560 -192 3420 0 + "1 up; 1 up+circle" x 60, 700 every 100 -> e1_N; 2500 every 250 -> e2_N)
//   story_ST13_s9a.bin --from e0 --teleport 2560 -192 3420 --yaw 0 --script "1 up; 1 up+circle" x 60 --frames 450
//   story_ST13_s9b --from e1_12300 --frames 500   story_ST13_s9c --from e2_12750 --frames 500 (script 9 to its end,
//   the guardian 0x484e00 spawned; 1 known failure: ST13 spawner 0x41c070 +9 at tick 13149, an enemy class)
//
//   story_ST13_s10a --from f0_13750 --frames 500   story_ST13_s10b --from f0_14250 --frames 400 (script 10)
//   (the guardian fight, content/manda/manda_guardian, 2026-10-10: the same chain re-run (fight from 12799); passive player:
//   story_ST13_g_p0 --from e2_12750 --frames 500   g_p1 --from e2_13250 --frames 500; the real kill, no hit-point
//   13684, script 10); story_ST13_g_k0 --from k_13000 --driver kill --slot 1 --frames 500   g_k1 --from k_13500
//   --driver kill --slot 1 --frames 300)
//   (ST13 / ST14, go_step: f0 tp 676 -192 -512 3072 + up/up+circle x 60 -> g0 (ST13/2); c1 tp -2212 -2064 0 1024 -> h0
//   (ST13/6); tp 1188 -192 3072 3072 -> h1 (8); tp 1700 -192 0 3072 -> h2 (9); tp 1188 -192 0 3072 -> h3 (10); tp 2300
//   -192 -1024 3072 + "30 up" + circle x 40 -> h4 (ST14/0); tp -4260 -192 1024 1024 -> i1; tp -1700 -192 0 1024 -> i2
//   (ST14/2); tp -1536 -192 -720 0 -> j0 (3); tp 1700 -192 -2048 3072 -> j1 (4); tp 2400 -192 0 3072 "60 up" 2500 every
//   250 -> k0_N (script 0x1a 14762..16469))
//   story_ST13_sub2 --from f0 --teleport 676 -192 -512 --yaw 3072 (up/up+circle x 60) --frames 300
//   story_ST13_sub6 --from h0 --frames 200   story_ST14_sub3 --from i2 --teleport -1536 -192 -720 --yaw 0 (same) --frames 300
//   story_ST14_s1a --from j1 --teleport 2400 -192 0 --yaw 3072 --script "60 up" --frames 250
//   story_ST14_s1a_N --from k0_N --frames 500 (N = 15250, 15750; 16250: 400): script 0x1a to its end (the guardian spawned)
//   story_ST14_sE.bin --from k0_17000 --poke 0xa6e680 2 --frames 400 (the guardian 0x48fb90 (slot 2) forced to its death
//   state 2 = flag 0x5b5 -> script 0xe; the boss itself is not ported)
//   story_ST14_key.bin --from m1 (force_state k0_17000 m1 2 3000) --teleport 512 64 120 --yaw 0 --script "5 up; 10 up+cross;
//   30 -" + "1 circle; 9 -" x 30 --frames 260 (the key item 0x5b12c0 -> flag 0x3d0, messages 0x16 / 0x17, out to ST24 sub 1)
//   (the Manda guardian fight, content/manda/manda_boss: ck = save 02 entered at --stage 14 --sub 4 (StoryRun(0x14, 4), one
//   frame, apply_save, story byte 0xa6d194 = 4), tick 150 teleport 2400 -192 0 yaw 3072 + 60 x up (script 0x1a, the
//   guardian spawned ~1630), checkpoints every 250 -> ck_N (tmp script, see story.md 13.7))
//   story_ST14_boss_1750 / _2000.bin --from ck_N.bin --frames 250 (the player passive)
//   story_ST14_bossq0.bin --from ck_2000 --teleport -512 -480 -1024 --yaw 0 --script "1 square; 3 -" x 100 --frames 400
//   story_ST14_bossq1.bin --from ck_2000 --teleport -512 -480 1024 --yaw 2048 (same script) --frames 400
//   story_ST14_bossq2.bin --from ck_2250 --teleport -512 -480 -1024 --yaw 1024 (same script) --frames 400
//   story_ST14_kill.bin --from ck_1750 --poke 0xa6e6e8 1 --poke 0xa6e6e9 0 --yaw 0 --script "1 square; 1 -" x 20
//   "; 260 -" --frames 300 (hit points 1: the guardian dies at 1767 -> flag 0x5b5 -> script 0xe)
//   --stage 12 --sub 5 --save DASH2-02.DAT --poke 0xa6d194 4 --poke 0xa6d195 4 (tick 201); explore s5b.bin 1500 --from
//   s5a --tp 0 -192 -350 0 --script "1 circle; 9 -" --repeat --every 250 -> s5b_N (chest 0x3e4 at 222, script 0xb
//   263..707, the fight);
//   0xc to 1708))
//   story_ST12_s5_s0b.bin --from s5a --teleport 0 -192 -350 --yaw 0 --script "1 circle; 9 -" x 7 "; 460 -" --frames 530
//   (handler 0x57e630, script 0xb, the cast 0x4875e0 variant 0, the falling blade 0x59c800 -> flag 0x680)
//   story_ST12_s5_g0 --from s5b_750 --frames 500   g1 --from s5b_1250 --frames 500 (passive: guardian variant 1 subs 0..3)
//   story_ST12_s5_h0 --from s5b_750 --poke 0xa6e57c 0x2c --poke 0xa6e57d 0x01 --frames 600 (hit points 300: sub 4, the
//   blades 0x59bee0 slide;
//   same run continued) --frames 400
//   story_ST12_s5_k0 --from s5b_750 --driver kill --slot 1 --frames 250 (the real kill: flag 0x5b3, script 0xc)
//   story_ST12_s5_sc0 --from k_1000 --frames 500   sc1 --from k_1500 --frames 300 (script 0xc to its end, record 0x89fe94)
//   (the blue card at the Manda ruin's terminal, ST12 sub 0, story.md 13.7 (fix/manda-terminal): python -m
//   --settle 30 --tp 0 -272 -1952 2048 --script "25 up; 5 -; 3 circle; 30 -; 3 circle; 30 -; 3 circle; 30 -; 3 circle"
//   --every 100 -> sf_N (the terminal's event volume, message 0x16, flag 0x5b6); explore x.bin 900 --from sf_100 --script
//   "1 circle; 9 -" x 6 --every 100 -> x_N (script 0xf 104..727))
//   story_ST12_sFa --from sf_100 --script "1 circle; 9 -" x 6 --frames 450 (the event, script 0xf, ST12 sub 6, sparks)
//   story_ST12_sFb --from x_500 --frames 300 (back to sub 0, the cells up, state 2: request 0xff, flag 0x59f, the end)
//   (after the Manda key, story.md 16;
//   sub), one frame, apply_save(save 02), pokes / flags, --settle N ticks, then teleport + pad script)
//   story_ST3E_s65.bin --from pre65 (ST3E sub 0, story 4 (0xa6d194 / 0xa6d195), flags 0x3d0 / 0x580 / 0x581, 29 ticks,
//   tp 0 0 1020 yaw 2048) --script "20 -; 1 circle; 30 -; 1 circle; 30 -; 1 circle; 10 -; 1 circle; 200 -" + "; 1 circle;
//   9 -" x 80 --frames 600 (the door to sub 6, script 0x65, request -> ST3E sub 1, story 5)
//   story_ST46_s31a.bin --save DASH2-02.DAT --stage 46 --sub 0 --poke 0xa6d194 5 --poke 0xa6d195 5 --flag 0x580 --flag
//   0x581 --frames 450 (handler 0x58a940 -> script 0x31);
//   story_ST46_s31b --from s31_500 --frames 450 (-> ST17 sub 0, script 0x3e)   story_ST17_s3ea --from s31_1000 --frames
//   600   story_ST17_s3eb --from s31_1500 --frames 750 (-> ST19 sub 0, story 6, handler 0x57fba0)
//   story_ST19_s32a.bin --save DASH2-02.DAT --stage 19 --sub 1 --poke 0xa6d194 6 --poke 0xa6d195 6 --flag 0x580 --flag
//   0x581 --flag 0x24 --flag 0x31 --frames 500 (handler 0x57fd10 -> script 0x32);
//   -> s32_N: story_ST19_s32b --from s32_500 --frames 450 (-> ST1B sub 2, handler 0x580270)
//   (the missions, story 6: save 02 entered at the room, --poke 0xa6d194 6 --poke 0xa6d195 6 --flag 0x580 --flag 0x581
//   --flag 0x24 --flag 0x31 --flag 0x25, --script "1 cross; 29 -" x 10 "; 1 -" --frames 300)
//   story_ST15_m0.bin (--stage 15 --sub 0: case 0x57eeb0, the opening FUN_0057ef70)   story_ST16_m0.bin (--stage 16:
//   case 0x57f040, FUN_0057f150, the gate lights 0x4309b0 variant 0))
//   (the mission ends: explore st15 / st16 3000 from the same start, "1 cross; 29 -" --repeat, every 250/500 -> st15_500 /
//   st16_500; explore s35pre 0 --from st15_500 --flag 0x28 (s34pre: st16_500, --flag 0x27); explore s35 / s34 3000 from the
//   same, every 250) story_ST15_s35a / story_ST16_s34a --from s35pre / s34pre --frames 500; story_ST15_s35b /
//   story_ST16_s34b --from s35_1000 / s34_1000 --frames 400 (scripts 0x35 / 0x34 -> ST19 sub 0)
//   story_ST17_s3d.bin --from st17pre (explore st17pre 0 --stage 17 --sub 0 + the mission flags + --flag 0x27 --flag 0x28)
//   --script "1 cross; 29 -" x 15 "; 1 -" --frames 450 (handler 0x57f2b0 later visit -> script 0x3d, the opening 0x1e);
//   explore st17 2000 from the same start, cross repeat, every 250; s36pre = st17_500 + --flag 0x29, explore s36 1000
//   every 250: story_ST17_s36a --from s36pre --frames 450   story_ST17_s36b --from s36_1000 --frames 450 (script 0x36
//   -> ST19 sub 0)
//   story_ST18_m0.bin --from st18pre (explore st18pre 0 --stage 18 --sub 0 + the mission flags + 0x27 / 0x28 / 0x29 /
//   0x30) --script "1 cross; 29 -" x 15 "; 1 -" --frames 450 (room function 0x5d8c30, handler 0x57f620: Glyde's ship,
//   the opening); explore st18 1500 from it (cross repeat, every 250); s37pre = st18_500 + --flag 0x2a, explore s37 4000
//   every 250: story_ST18_s37a --from s37pre --frames 600   story_ST18_s37b --from s37_1000 --frames 550 (script 0x37)
//   (the failure path -- flag 0x2f, event 0x1c -> ST46 sub 3, script 0x3c -> a task switch that reloads the save --
//   has no golden: script 0x3c is not ported)
//   (story 6 -> 7, 2026-10-11: the gunship 0x59f880 shot down in ST18 sub 0 after script 0x37 sets 0x2b; kk_5250 as in
//   test_actors act_ST18_gunship) story_ST18_s38.bin --from kk_5250 --script "1 r2+square; 2 r2" x 30 --frames 320 (the
//   kill, flag 0x2b, handler 0x57f620 state 4 -> story + 1, script 0x38 -> ST19 sub 0)
//   story_ST18_s7 / story_ST17_s7.bin --save DASH2-02.DAT --stage 18 / 17 --sub 0 --poke 0xa6d194 7 --poke 0xa6d195 7
//   + the mission flags (0x580 0x581 0x24 0x31 0x25 0x27 0x28 0x29 0x30 0x2a 0x2b) --frames 60 (handlers 0x57f7b0 / 0x57f3b0)
//   (story 7 -> 8, Calbania, story.md 17;
//   S = save 02 entered at the room + --poke 0xa6d194 N --poke 0xa6d195 N --poke 0xa73d70 0 --poke 0xa73d71 0)
//   story_ST1B_582.bin --from b7pre (S ST1B sub 2, N 7, flags 0x24 0x31 0x25 0x27 0x28 0x29 0x30 0x2a 0x2b 0x7b8,
//   --settle 40 --tp 106 -32 0 3072) --script "1 circle; 19 -" x 16 --frames 320 (walker message 0x5a "yes" -> 0x582)
//   story_ST01_cal.bin --from m0 (S ST01, N 7, the same flags + 0x580 0x582, 60 ticks) --script "97 right; 23 down;
//   20 -; 1 circle; 800 -" --frames 400 (map 7 -> ST23, story 8, script 0x57)   story_ST23_s57 --from m1_300 --frames 250
//   story_ST20_s10a --from c0 (m1_900 + 2) --teleport -3328 -736 5376 --yaw 0 --frames 400 (ST23 -> ST20, script 0x10)
//   story_ST20_s10b --from d0_1700 --script "1 circle; 19 -" x 20 --frames 400
//   (ST20 -> ST21: tp -1312 -640 1800 2048 + "1 up; 1 up+circle" x 45 -> e0; ST21 -> ST22: tp -3840 -1104 4352 0
//   "40 up" -> g0_0)   story_ST1F_s1da --from g0_0 --teleport 9984 -816 14592 --yaw 0 --script "40 up" --frames 500
//   (ST22 -> ST1F, script 0x1d); explore h1 from h0_0, "1 circle; 19 -" repeat, every 250: story_ST1F_s1db --from
//   h1_3500 / s1dc --from h1_4000 (same presses x 40) --frames 600 / 300
//   story_ST1F_s1ea --from p1e (h1_4250 + --flag 0x584) --frames 450 (script 0x1e); s1fa --from p1f (k1_4750 + 0x585)
//   --frames 620 (0x1f); s20a --from p20 (k2_5500 + 0x586) --frames 450 (0x20 -> ST30 sub 3); story_ST30_s21a / b
//   --from k3_5750 / k3_6250 --frames 500 / 600 (script 0x21 -> ST30 sub 0, handler 0x5854c0); story_ST30_s22 --from
//   p22 (k4 + --flag 0x20) --frames 400; story_ST30_sub1 / sub2 --from s1pre / s2pre (S ST30 sub 1 / 2, N 8, 0 ticks)
//   --script "1 circle; 19 -" x 25 --frames 450 (handlers 0x585670 / 0x585890 without flag 0x23)
//   story_ST30_gate --from s2xpre (as sub2 + --flag 0x23) --frames 600 (script 0x25 starts); explore x1 from it, the
//   same presses repeated, every 500: story_ST30_s25b --from x1_500 --frames 333 (script 0x25 to its request)
//   story_ST30_s25c --from x1_800 (x1 re-run with --every 100, 2026-10-11) --frames 300 (the reload to sub 2 at 834:
//   latch 0xa5a6d0 keeps script 0x25 from restarting; the escape: sound 0x29 gated by 0xa6d340, the timer, group 3)
// The draw pass (points 25 -> 26) is compared only for the sprite banner FUN_005f2690 (game/hud overlayDrawPass).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
// OD_TRACE_THROW=1: print the call stack of every C++ throw (vectored handler runs before the unwind)
static LONG CALLBACK traceThrow(EXCEPTION_POINTERS* e) {
    if (e->ExceptionRecord->ExceptionCode != 0xE06D7363) return EXCEPTION_CONTINUE_SEARCH;
    void* frames[24];
    const USHORT n = CaptureStackBackTrace(0, 24, frames, nullptr);
    HANDLE p = GetCurrentProcess();
    alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 256];
    for (USHORT i = 0; i < n; ++i) {
        auto* s = reinterpret_cast<SYMBOL_INFO*>(buf);
        s->SizeOfStruct = sizeof(SYMBOL_INFO);
        s->MaxNameLen = 255;
        DWORD64 d = 0;
        if (SymFromAddr(p, DWORD64(frames[i]), &d, s)) std::fprintf(stderr, "      %s+0x%llx\n", s->Name, (unsigned long long)d);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif
#include "check.hpp"
#include "data_source_binding.hpp"
#include "content/registry.hpp"
#include "engine/dat.hpp"
#include "engine/room.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actors.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/camera.hpp"
#include "game/display.hpp"
#include "game/encounter.hpp"
#include "translator/game_memory.hpp"
#include "game/hud.hpp"
#include "game/message.hpp"
#include "game/modern_play.hpp"
#include "game/text_script.hpp"
#include "game/player/player_model.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_update.hpp"
#include "game/room_scripts.hpp"
#include "game/story.hpp"
#include "game/fade.hpp"
#include "game/transition.hpp"
#include "golden_delta.hpp"
#include "translator/state_translator.hpp"

namespace fs = std::filesystem;
using od::port::ActorPool;
using od::port::kPlayer;

namespace {

// ---- the deterministic stream model ---------------------------------------------------
const int kBitrates[6][14] = {
    {32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320},   // MPEG-1 layer III
    {32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384},  // MPEG-1 layer II
    {32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448}, // MPEG-1 layer I
    {8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},       // MPEG-2 layer III
    {8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},       // MPEG-2 layer II
    {32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256},  // MPEG-2 layer I
};
uint32_t mp3Ticks(std::span<const uint8_t> d) {
    size_t p = 0;
    if (d.size() >= 10 && d[0] == 'I' && d[1] == 'D' && d[2] == '3')
        p = 10 + (size_t(d[6] & 0x7f) << 21 | size_t(d[7] & 0x7f) << 14 | size_t(d[8] & 0x7f) << 7 | size_t(d[9] & 0x7f));
    uint64_t samples = 0;
    int rate = 0;
    while (p + 4 <= d.size()) {
        const uint32_t h = uint32_t(d[p]) << 24 | uint32_t(d[p + 1]) << 16 | uint32_t(d[p + 2]) << 8 | d[p + 3];
        const uint32_t ver = (h >> 19) & 3, layer = (h >> 17) & 3, bri = (h >> 12) & 0xf, sri = (h >> 10) & 3,
                       pad = (h >> 9) & 1;
        if (((h >> 21) & 0x7ff) != 0x7ff || ver == 1 || layer == 0 || bri == 0 || bri == 15 || sri == 3) {
            ++p;
            continue;
        }
        const int row = (ver == 3 ? 0 : 3) + (layer == 1 ? 0 : layer == 2 ? 1 : 2);
        const int kbps = kBitrates[row][bri - 1] * 1000;
        static const int rates[4][3] = {{11025, 12000, 8000}, {0, 0, 0}, {22050, 24000, 16000}, {44100, 48000, 32000}};
        const int sr = rates[ver][sri];
        int len, spf;
        if (layer == 3) {
            len = (12 * kbps / sr + int(pad)) * 4;
            spf = 384;
        } else if (layer == 2) {
            len = 144 * kbps / sr + int(pad);
            spf = 1152;
        } else {
            spf = ver == 3 ? 1152 : 576;
            len = (ver == 3 ? 144 : 72) * kbps / sr + int(pad);
        }
        if (len <= 0) {
            ++p;
            continue;
        }
        samples += uint64_t(spf);
        rate = sr;
        p += size_t(len);
    }
    return rate ? uint32_t((samples * 30 + uint64_t(rate) - 1) / uint64_t(rate)) : 0;
}

// the stream slots live in the game image at kStreamTable: slot k at + k * 16 = {u32 used, u32 ticks, u32 loop, s32
// start tick or -1}
constexpr uint32_t kStreamTable = 0x7f100000;
struct ClockBackend : od::port::SoundBackend {
    od::GameMem* m = nullptr;
    fs::path gameDir;
    uint32_t tick = 0;
    uint32_t slot(uint32_t k) const { return kStreamTable + k * 16; }
    uint32_t createStream(std::span<const uint8_t> mp3, uint32_t loop) override {
        for (uint32_t k = 1; k < 8; ++k) // FUN_0040ab60: the first free slot 1..7
            if (m->u32(slot(k)) == 0) {
                m->set32(slot(k), 1);
                m->set32(slot(k) + 4, mp3Ticks(mp3));
                m->set32(slot(k) + 8, loop);
                m->set32(slot(k) + 12, 0xffffffffu);
                return k;
            }
        return 0;
    }
    void destroyStream(uint32_t s) override {
        if (s >= 1 && s < 8)
            for (uint32_t k = 0; k < 16; k += 4) m->set32(slot(s) + k, 0);
    }
    void streamPlay(uint32_t s) override {
        if (s >= 1 && s < 8 && m->u32(slot(s))) m->set32(slot(s) + 12, tick);
    }
    void streamStop(uint32_t s) override {
        if (s >= 1 && s < 8 && m->u32(slot(s))) m->set32(slot(s) + 12, 0xffffffffu);
    }
    bool streamPlaying(uint32_t s) override {
        if (s < 1 || s >= 8 || !m->u32(slot(s)) || m->s32(slot(s) + 12) < 0) return false;
        return m->u32(slot(s) + 8) != 0 || int64_t(tick) - m->s32(slot(s) + 12) < int64_t(m->u32(slot(s) + 4));
    }
    std::vector<uint8_t> readVoiceTrack(const std::string& file, uint32_t track) override {
        const auto f = od::readFile(gameDir / "snd" / file);
        if (f.size() < (track + 2) * 4) return {};
        const uint32_t a = od::rd32(f.data() + track * 4), b = od::rd32(f.data() + track * 4 + 4);
        if (b < a || b > f.size()) return {};
        return std::vector<uint8_t>(f.begin() + long(a), f.begin() + long(b));
    }
};

// ---- golden (tests/golden_delta.hpp: STG1 = the TRG1 layout) --------------------------------------------------
using od::test::Meta;
using od::test::Range;
using od::test::Rec;
using Golden = od::test::DeltaGolden;
const char* kPointName[] = {"field entry", "event runner", "room scripts", "after room scripts", "frame body",
                            "script runner", "pool e0", "player input", "pool 20", "after pool 20", "after pool 40",
                            "after pool a0", "after pool 60", "after collision", "after player", "after pool 10",
                            "after camera", "after a0 late", "after 60 late", "after e0 late", "frame end",
                            "mode end", "window logic", "after window logic", "after layout", "draw pass", "render"};

bool readGolden(const fs::path& path, od::GameMem& m, Golden& g) {
    return od::test::readDeltaGolden(path, "STG1", m, g, false);
}

struct Runner {
    od::GameMem& m;
    const Golden& g;
    fs::path datDir;
    int maxReports;
    od::port::PlayerEnv env;
    std::map<int, od::GameFile> stFiles;
    uint32_t roomKey = 0xffffffff;
    size_t cursor = 0;
    int reported = 0;
    bool tickOk = true;
    std::map<std::string, int> stepFails;
    std::function<bool(uint32_t)> skip; // the current step's extra "not compared" filter

    const od::GameFile& stFile(int stage) {
        auto it = stFiles.find(stage);
        if (it != stFiles.end()) return it->second;
        char name[16];
        std::snprintf(name, sizeof name, "ST%02X.DAT", stage);
        return stFiles[stage] = od::pcGameFile(name, od::readFile(datDir / name));
    }
    void bindRoom(const Rec& r) {
        const int stage = m.u8(0xa6d190);
        const uint32_t pool = m.u32(0x6d0084);
        const uint32_t key = uint32_t(stage) << 24 ^ pool ^ (uint32_t(m.u8(0x6d0094)) << 8) ^ m.u8(0x6d0095) ^
                             (m.u32(0x6d0088) * 0x9e3779b1u);
        if (key != roomKey && pool != 0) {
            const auto block = od::roomBlock(stFile(stage));
            for (const od::RoomGrid& gr : od::roomGrids(block)) {
                if (gr.x0 != m.u8(0x6d0094) || gr.z0 != m.u8(0x6d0095)) continue;
                const od::CollisionRoom probe = od::collisionRoom(block, gr);
                const uint32_t base = pool - uint32_t(probe.pool);
                if (base + probe.segments != m.u32(0x6d008c) || base + probe.cells != m.u32(0x6d0088)) continue;
                m.map(base, block);
                const auto [shared, origin] = m.sharedAt(base, uint32_t(block.size()));
            env.room = od::collisionRoom(shared.share(), gr, origin); // the image's block itself (a copy detaches on the first write)
                break;
            }
            roomKey = key;
        }
        if (!r.seg.empty() && r.segAddr) m.write(r.segAddr, r.seg.data(), uint32_t(r.seg.size()));
    }
    void load(const Rec& r) {
        od::test::storeNativeState(m);
        size_t o = 0;
        for (const Range& rg : g.ranges) {
            m.write(rg.addr, r.blob.data() + o, rg.size);
            o += rg.size;
        }
        bindRoom(r);
        od::test::loadNativeState(m);
    }
    static bool ignored(uint32_t a) {
        if ((a >= 0xa714c4 && a < 0xa714c8) || (a >= 0xa79fe0 && a < 0xa79ff0)) return true; // file loader
        if (a >= 0xa5eed8 && a < 0xa5eedc) return true; // the voice clip's malloc'd MP3 copy (a heap pointer)
        if (a == kPlayer + 0x86 || a == kPlayer + 0x87) return true;                           // matrix pad
        if (a >= 0xa6bcb8 && a < 0xa6bcbc) return true;                                        // uninitialised local
        if (a >= kPlayer + 0x2a4 && a < kPlayer + 0x500) return true;                          // bone pose
        if ((a >= 0x6d0040 && a < 0x6d0080) || (a >= 0x6d0090 && a < 0x6d0094) || (a >= 0xa6bb88 && a < 0xa6bb8c) ||
            (a >= 0x6d00c0 && a < 0x6d0200) || (a >= 0xa711b0 && a < 0xa71200) || (a >= 0xa599e4 && a < 0xa59a00) ||
            (a >= 0xa59a04 && a < 0xa59a50) || (a >= 0xa59a54 && a < 0xa59a60))
            return true; // render side (see test_transition)
        if (a >= 0xa6e3a0 && a < 0xa6e3a0 + 24 * 0x16c) { // pool 20: the stack-garbage tail of +0x74
            const uint32_t o = (a - 0xa6e3a0) % 0x16c;
            if (o >= 0x86 && o < 0x94) return true;
        }
        if (a >= 0xa72660 && a < 0xa72660 + 0x1600) { // bone blocks: matrix pads, the unwritten tail
            const uint32_t o = (a - 0xa72660) % 0xb0;
            if (o < 0x80 ? (o & 0x1f) >= 0x12 : (o >= 0xa0 || (o & 7) >= 6)) return true;
        }
        return false;
    }
    // the golden's byte at a (false: not recorded)
    bool goldenU8(const Rec& want, uint32_t a, uint8_t& out) const {
        size_t o = 0;
        for (const Range& rg : g.ranges) {
            if (a >= rg.addr && a < rg.addr + rg.size) {
                out = want.blob[o + (a - rg.addr)];
                return true;
            }
            o += rg.size;
        }
        return false;
    }
    bool goldenS16(const Rec& want, uint32_t a, int16_t& out) const {
        uint8_t lo, hi;
        if (!goldenU8(want, a, lo) || !goldenU8(want, a + 1, hi)) return false;
        out = int16_t(uint16_t(lo | hi << 8));
        return true;
    }
    // The exe's stale stack the port takes as 0 (documented deviations, never reproducible):
    //  * the tanks (pool 20 type 0x40: ST1F sub 0, content/calbania/st1f_birdbot_tank tankHead; Glyde's sub 1,
    //    glyde_base_tank aimTurret): the turret yaw +0x2e off by one; then that actor's pose (its bone blocks) and a
    //    shell it fires (pool 40 yaw +0x2a, +-1) follow it
    //  * the mission gunships (static type 0x1b, content/nino/mission_gunships angleFromPoint): on the tick the
    //    bearing lands at a whole number's edge the steer dead zone differs: spin.y +0x4a 0 against +-0x10
    std::vector<std::pair<uint32_t, uint32_t>> staleStackSkips(const Rec& want) const {
        std::vector<std::pair<uint32_t, uint32_t>> out; // [from, to)
        bool turret = false;
        for (uint32_t i = 0; i < 24; ++i) {
            const uint32_t a = 0xa6e3a0 + i * 0x16c;
            int16_t w;
            if (m.u8(a + 4) != 0x40 || m.u8(a + 5) > 1 || !goldenS16(want, a + 0x2e, w)) continue;
            const int d = int(m.s16(a + 0x2e)) - int(w);
            if (d != 1 && d != -1) continue;
            turret = true;
            out.push_back({a + 0x2e, a + 0x30});
            const uint32_t mask = m.u32(a + 0xcc);
            for (int k = 31; k >= 0; --k)
                if (mask & (1u << k)) out.push_back({0xa72660 + uint32_t(31 - k) * 0xb0, 0xa72660 + uint32_t(32 - k) * 0xb0});
        }
        if (turret)
            for (uint32_t i = 0; i < 32; ++i) {
                const uint32_t a = 0xa78420 + i * 0xdc;
                int16_t w;
                if (!goldenS16(want, a + 0x2a, w)) continue;
                const int d = int(m.s16(a + 0x2a)) - int(w);
                if (d == 1 || d == -1) out.push_back({a + 0x2a, a + 0x2c});
            }
        for (uint32_t i = 0; i < 64; ++i) {
            const uint32_t a = 0xa67860 + i * 0xcc;
            int16_t w;
            if (m.u8(a + 4) != 0x1b || !goldenS16(want, a + 0x4a, w) || w != 0) continue;
            if (m.s16(a + 0x4a) == 0x10 || m.s16(a + 0x4a) == -0x10) out.push_back({a + 0x4a, a + 0x4c});
        }
        return out;
    }
    int compare(const Rec& want, const char* step) {
        od::test::storeNativeState(m);
        const auto stale = staleStackSkips(want);
        int diffs = 0;
        size_t o = 0;
        for (const Range& rg : g.ranges) {
            for (uint32_t k = 0; k < rg.size; ++k) {
                const uint32_t a = rg.addr + k;
                const uint8_t got = m.u8(a), w = want.blob[o + k];
                if (got == w || ignored(a) || (skip && skip(a))) continue;
                if (std::any_of(stale.begin(), stale.end(), [a](const auto& r) { return a >= r.first && a < r.second; }))
                    continue;
                if (a >= 0xa67860 && a < 0xa67860 + 64 * 0xcc && m.u8(0xa6d190) == 0x14 &&
                    m.u8(a - (a - 0xa67860) % 0xcc + 4) == 0x2c) {
                    // the ST14 beam's segments (0x5ab210): +0x86 the pad short Maybe_MulMatrix3x3 0x40bc90 copies
                    // from its uninitialised 10th local; +0xa4..+0xb3 the projected corners (FUN_005ab320, render)
                    const uint32_t off = (a - 0xa67860) % 0xcc;
                    if (off == 0x86 || off == 0x87 || (off >= 0xa4 && off < 0xb4)) continue;
                }
                if (a >= 0xa6c6e0 && a < 0xa6c6e0 + 64 * 0x28 && ((a - 0xa6c6e0) % 0x28 == 0x16 ||
                                                                   (a - 0xa6c6e0) % 0x28 == 0x17) &&
                    m.u8(a - (a - 0xa6c6e0) % 0x28 + 4) == 0x12)
                    continue; // fire effects: Data's update 0x4e86a0 copies 8 bytes of a 6-byte local (stack garbage)
                if (diffs < 12 && reported < maxReports)
                    std::fprintf(stderr, "  tick %u %s @%s 0x%06x: want %02x got %02x\n", want.tick, step,
                                 kPointName[want.point], a, w, got);
                ++diffs;
            }
            o += rg.size;
        }
        if (diffs) {
            tickOk = false;
            ++stepFails[step];
        }
        return diffs;
    }
    // the next record must be `point`: compare it (optionally) and return it
    const Rec& expect(uint8_t point, const char* step, bool compareIt = true) {
        if (cursor >= g.recs.size() || g.recs[cursor].point != point) {
            if (reported < maxReports)
                std::fprintf(stderr, "  %s: expected point %s, golden has %s\n", step, kPointName[point],
                             cursor < g.recs.size() ? kPointName[g.recs[cursor].point] : "end");
            tickOk = false;
            throw std::runtime_error("point sequence");
        }
        const Rec& r = g.at(cursor++);
        if (compareIt) compare(r, step);
        return r;
    }
};

} // namespace

int main(int argc, char** argv) {
    od::content::registerAll(); // the game code dispatches into the content through its registry
    if (argc < 4) return 2;
#ifdef _WIN32
    if (std::getenv("OD_TRACE_THROW")) {
        SymInitialize(GetCurrentProcess(), nullptr, TRUE);
        AddVectoredExceptionHandler(1, traceThrow);
    }
#endif
    const fs::path datDir = argv[1], exe = argv[2], data = argv[3];
    od::bindTestDataSource(exe);
    od::port::setModernPlay(false); // the goldens hold the exe's gameplay (game/modern_play)
    const int maxReports = argc > 4 ? std::stoi(argv[4]) : 3;
    // filter: comma-separated substrings, a golden runs when its name contains any of them (CMakeLists registers one
    // test per golden group with the group's file names, so ctest -j runs the groups in parallel)
    std::vector<std::string> filters;
    for (std::string rest = argc > 5 ? argv[5] : ""; ;) {
        const size_t c = rest.find(',');
        filters.push_back(rest.substr(0, c));
        if (c == std::string::npos) break;
        rest = rest.substr(c + 1);
    }
    std::vector<fs::path> files;
    for (const auto& de : fs::directory_iterator(data)) {
        const std::string name = de.path().filename().string();
        if (de.path().extension() != ".bin" || name.rfind("story_", 0) != 0) continue;
        if (std::any_of(filters.begin(), filters.end(), [&](const std::string& f) { return name.find(f) != std::string::npos; }))
            files.push_back(de.path());
    }
    std::sort(files.begin(), files.end());
    CHECK(!files.empty());
    for (const fs::path& path : files) {
        od::test::resetNativeActors(); // the previous golden's actors are not this one's
        od::GameMem m;
        const od::ScopedGameMemory bound(m);
        m.mapExeData(exe);
        m.mapDatPsx(od::readFile(datDir / "PL00P000.DAT"));
        Golden g;
        if (!readGolden(path, m, g)) {
            std::fprintf(stderr, "%s: not a STG1 golden\n", path.filename().string().c_str());
            CHECK(false);
            continue;
        }
        for (const Range& r : g.ranges)
            if (!m.mapped(r.addr, r.size)) m.mapZero(r.addr, r.size);
        od::test::loadNativeState(m); // the native state from this golden's image (nothing of the previous one)
        ClockBackend clock;
        clock.m = &m;
        clock.gameDir = exe.parent_path();
        od::port::setSoundBackend(&clock);
        Runner run{m, g, datDir, maxReports};
        od::port::ActorContext actors;
        actors.log = [&](const std::string& msg) { // which class was disabled (with the failing tick's report)
            if (run.reported < maxReports && msg.find("class disabled") != std::string::npos)
                std::fprintf(stderr, "  %s\n", msg.c_str());
        };
        od::port::FadeTask fade;
        // message fb 3c (FUN_0055b1b0) starts the screen fade 0x53f6b0 as the app does (main.cpp)
        od::port::setMessageFadeHook([&](uint8_t type) { od::port::fadeStart(fade, type, 0); });
        od::port::TransitionHooks hooks;
        // a pool loop runs (and its result is compared) when every live actor it would update has a ported class;
        // otherwise the game's state after the loop is taken as it is
        auto poolPorted = [&](ActorPool p) {
            struct Def {
                uint32_t base, stride, n;
            };
            Def d{};
            switch (p) {
            case ActorPool::Controllers: case ActorPool::ControllersLate: d = {0xa6bfa0, 0x20, 32}; break;
            case ActorPool::Animated: d = {0xa6e3a0, 0x16c, 24}; break;
            case ActorPool::Projectiles: d = {0xa78420, 0xdc, 32}; break;
            case ActorPool::Effects: case ActorPool::EffectsLate: d = {0xa6c6e0, 0x28, 64}; break;
            case ActorPool::Statics: case ActorPool::StaticsLate: d = {0xa67860, 0xcc, 64}; break;
            case ActorPool::Shots: d = {0xa6ab80, 0x100, 16}; break;
            }
            for (uint32_t i = 0; i < d.n; ++i) {
                const od::RoutineRef h = od::port::actorHandler(od::port::actorHeaderAt(d.base + i * d.stride), p);
                if (h && !od::port::portedActorClass(h.id())) {
                    static std::set<uint32_t> told; // OD_LOG_UNPORTED=1: name the classes taken from the golden
                    if (std::getenv("OD_LOG_UNPORTED") && told.insert(h.pointerValue()).second)
                        std::fprintf(stderr, "  unported class 0x%06x (pool %d) -- the pool is loaded from the golden\n",
                                     h.pointerValue(), int(p));
                    return false;
                }
            }
            return true;
        };
        const char* doing = "";
        auto pool = [&](ActorPool p, uint8_t after, const char* name) {
            doing = name;
            if (poolPorted(p)) {
                od::port::updatePool(run.env, actors, p);
                run.load(run.expect(after, name));
            } else {
                run.load(run.expect(after, name, false));
            }
        };
        hooks.roomHandler = [&] { od::port::roomScriptHandler(); };
        hooks.roomScripts = [&] {
            run.expect(1, "field mode start", false);
            run.expect(2, "event runner", false);
            run.expect(3, "event + room scripts");
        };
        hooks.frameStart = [&] {
            run.load(run.expect(4, "frame start", false));
            doing = "script runner";
        };
        hooks.frame = [&] {
            // the attract wrap-ups (content/intro/attract) end the game task inside the script runner: *0xa7a000 = 2 and
            // FUN_00604b06 switches to Maybe_MainTaskEntry, so the golden goes on at the window pass (point 22) --
            // compare the state there and end the tick (story_AT_skip)
            od::test::storeNativeState(m);
            if (m.u32(0xa7a000) == 2 && run.cursor < g.recs.size() && g.recs[run.cursor].point == 22) {
                run.expect(22, "script runner (task switch to the title)");
                throw std::runtime_error("task switch");
            }
            run.load(run.expect(5, "script runner"));
            doing = "encounter tick 0x50f350";
            od::port::encounterTick(run.env); // FUN_0050f350 (game/encounter)
            doing = "breakable walls 0x58cc10";
            od::port::breakableWallsTick(); // exe 0x58cc10 (game/breakable_walls)
            run.load(run.expect(6, "encounter tick 0x50f350 + breakable walls 0x58cc10"));
            pool(ActorPool::Controllers, 7, "pool e0");
            doing = "player input";
            od::port::playerInputAndDispatch(run.env);
            run.load(run.expect(8, "player input"));
            pool(ActorPool::Animated, 9, "pool 20");
            pool(ActorPool::Projectiles, 10, "pool 40");
            pool(ActorPool::Effects, 11, "pool a0");
            pool(ActorPool::Statics, 12, "pool 60");
            doing = "actor collision";
            od::port::actorCollision();
            run.load(run.expect(13, "actor collision"));
            doing = "player update";
            od::port::playerUpdate(run.env);
            run.load(run.expect(14, "player update"));
            pool(ActorPool::Shots, 15, "pool 10");
            doing = "camera";
            od::port::cameraUpdate();
            run.load(run.expect(16, "camera"));
            pool(ActorPool::EffectsLate, 17, "pool a0 late");
            pool(ActorPool::StaticsLate, 18, "pool 60 late");
            pool(ActorPool::ControllersLate, 19, "pool e0 late");
            run.load(run.expect(20, "frame end (hit list ...)", false));
        };
        hooks.render = [&] {
            // the draw pass (point 25 -> 26): only its sprite banner FUN_005f2690 (game/hud overlayDrawPass, the
            // animation state 0xa6bdb0..) is ported and compared; the rest of the pass is render output
            run.load(run.expect(25, "draw pass", false));
            // exe 0x5422f0 returns at once while drawing is off: no banner, point 26 still comes after the pass
            if (od::port::display().drawingOff == 0) od::hud::overlayDrawPass();
            run.skip = [](uint32_t a) { return a < 0xa6bdb0 || a >= 0xa6beb0; };
            run.load(run.expect(26, "draw pass sprite banner 0x5f2690"));
            run.skip = nullptr;
        };
        int ticks = 0, failed = 0;
        std::vector<uint16_t> playerDats; // the player files the game holds (0xa6d1d8 / 0xa6d1da, game/player/player_model)
        while (run.cursor < g.recs.size()) {
            const Meta& r = g.recs[run.cursor];
            if (r.point != 0) {
                ++run.cursor;
                continue;
            }
            run.tickOk = true;
            clock.tick = r.tick;
            try {
                ++run.cursor;
                run.load(g.at(run.cursor - 1));
                if (const std::vector<uint16_t> d = od::playerDatLoaded(); d != playerDats) {
                    for (uint16_t i : d) m.mapDatPsx(od::readFile(datDir / od::datFileName(i)));
                    playerDats = d;
                }
                actors.disabled.clear();
                doing = "field mode (event runner / room scripts)";
                const uint16_t room = m.u16(0xa6d190);
                od::port::fieldMode(run.env, hooks, fade);
                od::test::storeNativeState(m);
                // a sub-room change inside the field mode (0x5417bb..: FUN_00542370, the script runner, the room
                // re-initialisation 0x5d5b30 with its per-stage setup, which has no hook here): test_transition's
                // ground; take the game's state at the mode end
                if (m.u16(0xa6d190) != room) run.load(run.expect(21, "mode handler (sub-room change)", false));
                else run.expect(21, "mode handler");
                // the game task loop 0x541070 yields (0x604ad9) after the mode handler; on the tick a room exits the
                // golden can hold no window pass before the next task iteration (another mode handler, point 21, or
                // the next field entry): the rest of the tick is the transition, not compared here (test_transition)
                // (also when the golden ends there: its last tick stopped before the next task iteration)
                if (run.cursor >= g.recs.size() || g.recs[run.cursor].point != 22) {
                    while (run.cursor < g.recs.size() && g.recs[run.cursor].point != 0) ++run.cursor;
                    ++ticks;
                    if (!run.tickOk) {
                        ++failed;
                        ++run.reported;
                    }
                    continue;
                }
                // WinMain after the game task: FUN_005578a0 (window logic + layout)
                run.load(run.expect(22, "window pass", false));
                od::port::messageTick();
                run.expect(23, "window logic", false);
                run.expect(24, "windows");
            } catch (const std::exception& e) {
                const bool taskSwitch = std::string(e.what()) == "task switch"; // the game task ended (hooks.frame)
                if (std::string(e.what()) != "point sequence" && !taskSwitch && run.reported < maxReports)
                    std::fprintf(stderr, "  tick %u (%s): %s\n", r.tick, doing, e.what());
                if (!taskSwitch) run.tickOk = false;
                while (run.cursor < g.recs.size() && g.recs[run.cursor].point != 0) ++run.cursor;
            }
            if (!actors.disabled.empty()) {
                if (run.reported < maxReports) std::fprintf(stderr, "  tick %u: an actor class was disabled\n", r.tick);
                run.tickOk = false;
            }
            ++ticks;
            if (!run.tickOk) {
                ++failed;
                ++run.reported;
            }
        }
        std::printf("%s: %d ticks: %d ok, %d failing\n", path.filename().string().c_str(), ticks, ticks - failed, failed);
        for (const auto& [step, n] : run.stepFails) std::printf("  %s: %d ticks differ\n", step.c_str(), n);
        od::port::setSoundBackend(nullptr);
        CHECK(failed == 0);
    }
    return testResult("test_story");
}
