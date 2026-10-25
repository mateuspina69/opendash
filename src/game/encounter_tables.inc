// Do not edit.
// What the stage setups' encounter code does; the tables it points the game at are the data source's (the PC
// source: engine/pc_encounter_tables.inc, engine/pc_room_tables.inc).
// kEncStages: per stage {stage setup [0x8d3808 + stage * 4] (its routine), the argument of exe 0x50f280 (the encounter
//   kinds; -1 none), layout tree root (-1 none)}
// kEncLayoutOps: {op, flag, global, next[4]} -- LayoutOp: End, ClearFlag (exe 0x510e30), SetFlag
//   (exe 0x510e10), TestFlag (exe 0x510e50: next[0] clear / next[1] set), RngStep 0x6d0000 (next[r & 3]),
//   Write the global = the table TableId::EncounterLayoutTable [the op's index]
// kEncStageRest: the other effects of the 4 RNG setups in exe order {stage, EncounterRestKind (Write: a dword
//   write / Call), the global written (None for a call), argument 0, argument 1 (a call's), the callee / the
//   function pointer written (its routine)}; a write of a table: the stage's table (TableId::Stage*)
static constexpr EncStage kEncStages[] = {
    {RoutineId::St00StageSetup, 1, -1}, // ST00
    {RoutineId::St01StageSetup, 0, -1}, // ST01
    {RoutineId::St02StageSetup, 0, -1}, // ST02
    {RoutineId::St03StageSetup, 0, -1}, // ST03
    {RoutineId::St04StageSetup, 0, -1}, // ST04
    {RoutineId::St05StageSetup, 0, -1}, // ST05
    {RoutineId::St06StageSetup, 0, -1}, // ST06
    {RoutineId::St07StageSetup, 0, -1}, // ST07
    {RoutineId::St08StageSetup, 0, -1}, // ST08
    {RoutineId::St09StageSetup, 0, -1}, // ST09
    {RoutineId::St0AStageSetup, 0, -1}, // ST0A
    {RoutineId::St0BStageSetup, 0, -1}, // ST0B
    {RoutineId::St0CStageSetup, 0, -1}, // ST0C
    {RoutineId::St0DStageSetup, 18, -1}, // ST0D
    {RoutineId::St0EStageSetup, -1, -1}, // ST0E
    {RoutineId::St0FStageSetup, 1, -1}, // ST0F
    {RoutineId::St10StageSetup, 21, -1}, // ST10
    {RoutineId::St11StageSetup, 16, -1}, // ST11
    {RoutineId::St12StageSetup, 5, -1}, // ST12
    {RoutineId::St13StageSetup, 5, -1}, // ST13
    {RoutineId::St14StageSetup, 5, -1}, // ST14
    {RoutineId::St15StageSetup, 20, -1}, // ST15
    {RoutineId::St16StageSetup, 20, -1}, // ST16
    {RoutineId::St17StageSetup, 20, -1}, // ST17
    {RoutineId::St18StageSetup, 4, -1}, // ST18
    {RoutineId::St19StageSetup, 0, -1}, // ST19
    {RoutineId::St1AStageSetup, 0, -1}, // ST1A
    {RoutineId::St1BStageSetup, 0, -1}, // ST1B
    {RoutineId::St1CStageSetup, 0, -1}, // ST1C
    {RoutineId::St1DStageSetup, 16, -1}, // ST1D
    {RoutineId::St1EStageSetup, 0, -1}, // ST1E
    {RoutineId::St1FStageSetup, 20, -1}, // ST1F
    {RoutineId::St20StageSetup, 0, -1}, // ST20
    {RoutineId::St21StageSetup, 0, -1}, // ST21
    {RoutineId::St22StageSetup, 18, -1}, // ST22
    {RoutineId::St23StageSetup, 0, -1}, // ST23
    {RoutineId::St24StageSetup, 0, -1}, // ST24
    {RoutineId::St25StageSetup, 0, -1}, // ST25
    {RoutineId::St26StageSetup, 21, -1}, // ST26
    {RoutineId::St27StageSetup, 21, -1}, // ST27
    {RoutineId::St28StageSetup, 23, -1}, // ST28
    {RoutineId::St29StageSetup, 4, -1}, // ST29
    {RoutineId::St2AStageSetup, 4, -1}, // ST2A
    {RoutineId::St2BStageSetup, 5, -1}, // ST2B
    {RoutineId::St2CStageSetup, 0, -1}, // ST2C
    {RoutineId::St2DStageSetup, 20, -1}, // ST2D
    {RoutineId::St2EStageSetup, 4, -1}, // ST2E
    {RoutineId::St2FStageSetup, 22, -1}, // ST2F
    {RoutineId::St30StageSetup, 20, -1}, // ST30
    {RoutineId::St31StageSetup, 16, -1}, // ST31
    {RoutineId::St32StageSetup, 0, -1}, // ST32
    {RoutineId::St33StageSetup, 17, -1}, // ST33
    {RoutineId::St34StageSetup, 21, -1}, // ST34
    {RoutineId::St35StageSetup, 0, -1}, // ST35
    {RoutineId::St36StageSetup, 5, -1}, // ST36
    {RoutineId::St37StageSetup, 5, -1}, // ST37
    {RoutineId::St38StageSetup, 1, -1}, // ST38
    {RoutineId::St39StageSetup, 0, -1}, // ST39
    {RoutineId::St3AStageSetup, 0, -1}, // ST3A
    {RoutineId::St3BStageSetup, 5, -1}, // ST3B
    {RoutineId::St3CStageSetup, 0, -1}, // ST3C
    {RoutineId::St3DStageSetup, 0, -1}, // ST3D
    {RoutineId::St3EStageSetup, 0, -1}, // ST3E
    {RoutineId::St3FStageSetup, 0, -1}, // ST3F
    {RoutineId::St40StageSetup, 0, -1}, // ST40
    {RoutineId::St41StageSetup, 5, -1}, // ST41
    {RoutineId::St42StageSetup, 5, -1}, // ST42
    {RoutineId::St43StageSetup, 0, -1}, // ST43
    {RoutineId::St44StageSetup, 4, -1}, // ST44
    {RoutineId::St45StageSetup, 4, -1}, // ST45
    {RoutineId::St46StageSetup, 0, -1}, // ST46
    {RoutineId::St47StageSetup, 0, -1}, // ST47
    {RoutineId::St48StageSetup, -1, -1}, // ST48
    {RoutineId::St49StageSetup, 0, -1}, // ST49
    {RoutineId::St4AStageSetup, 17, -1}, // ST4A
    {RoutineId::St4BStageSetup, 0, -1}, // ST4B
    {RoutineId::St4CStageSetup, 0, -1}, // ST4C
    {RoutineId::St4DStageSetup, 17, 0}, // ST4D
    {RoutineId::St4EStageSetup, 21, 20}, // ST4E
    {RoutineId::St4FStageSetup, 21, 40}, // ST4F
    {RoutineId::St50StageSetup, 21, 116}, // ST50
    {RoutineId::St51StageSetup, 21, -1}, // ST51
    {RoutineId::St52StageSetup, 0, -1}, // ST52
    {RoutineId::St53StageSetup, 0, -1}, // ST53
    {RoutineId::St54StageSetup, 0, -1}, // ST54
    {RoutineId::St55StageSetup, 20, -1}, // ST55
    {RoutineId::St56StageSetup, 0, -1}, // ST56
    {RoutineId::St57StageSetup, 0, -1}, // ST57
    {RoutineId::St58StageSetup, 0, -1}, // ST58
    {RoutineId::St59StageSetup, 0, -1}, // ST59
    {RoutineId::St5AStageSetup, 0, -1}, // ST5A
    {RoutineId::St5BStageSetup, 0, -1}, // ST5B
    {RoutineId::St5CStageSetup, 0, -1}, // ST5C
};
static constexpr EncLayoutOp kEncLayoutOps[] = {
    {LayoutOp::ClearFlag, 0xb0, StageGlobal::None, {1, -1, -1, -1}}, // 0
    {LayoutOp::ClearFlag, 0xb1, StageGlobal::None, {2, -1, -1, -1}}, // 1
    {LayoutOp::ClearFlag, 0xb2, StageGlobal::None, {3, -1, -1, -1}}, // 2
    {LayoutOp::RngStep, 0x0, StageGlobal::None, {4, 8, 12, 16}}, // 3
    {LayoutOp::SetFlag, 0xb0, StageGlobal::None, {5, -1, -1, -1}}, // 4
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {6, -1, -1, -1}}, // 5
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {7, -1, -1, -1}}, // 6
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 7
    {LayoutOp::SetFlag, 0xb1, StageGlobal::None, {9, -1, -1, -1}}, // 8
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {10, -1, -1, -1}}, // 9
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {11, -1, -1, -1}}, // 10
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 11
    {LayoutOp::SetFlag, 0xb1, StageGlobal::None, {13, -1, -1, -1}}, // 12
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {14, -1, -1, -1}}, // 13
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {15, -1, -1, -1}}, // 14
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 15
    {LayoutOp::SetFlag, 0xb2, StageGlobal::None, {17, -1, -1, -1}}, // 16
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {18, -1, -1, -1}}, // 17
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {19, -1, -1, -1}}, // 18
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 19
    {LayoutOp::ClearFlag, 0xb0, StageGlobal::None, {21, -1, -1, -1}}, // 20
    {LayoutOp::ClearFlag, 0xb1, StageGlobal::None, {22, -1, -1, -1}}, // 21
    {LayoutOp::ClearFlag, 0xb2, StageGlobal::None, {23, -1, -1, -1}}, // 22
    {LayoutOp::RngStep, 0x0, StageGlobal::None, {24, 28, 32, 36}}, // 23
    {LayoutOp::SetFlag, 0xb0, StageGlobal::None, {25, -1, -1, -1}}, // 24
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {26, -1, -1, -1}}, // 25
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {27, -1, -1, -1}}, // 26
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 27
    {LayoutOp::SetFlag, 0xb1, StageGlobal::None, {29, -1, -1, -1}}, // 28
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {30, -1, -1, -1}}, // 29
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {31, -1, -1, -1}}, // 30
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 31
    {LayoutOp::SetFlag, 0xb2, StageGlobal::None, {33, -1, -1, -1}}, // 32
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {34, -1, -1, -1}}, // 33
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {35, -1, -1, -1}}, // 34
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 35
    {LayoutOp::SetFlag, 0xb0, StageGlobal::None, {37, -1, -1, -1}}, // 36
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {38, -1, -1, -1}}, // 37
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {39, -1, -1, -1}}, // 38
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 39
    {LayoutOp::ClearFlag, 0xb0, StageGlobal::None, {41, -1, -1, -1}}, // 40
    {LayoutOp::ClearFlag, 0xb1, StageGlobal::None, {42, -1, -1, -1}}, // 41
    {LayoutOp::ClearFlag, 0xb2, StageGlobal::None, {43, -1, -1, -1}}, // 42
    {LayoutOp::RngStep, 0x0, StageGlobal::None, {44, 62, 80, 98}}, // 43
    {LayoutOp::SetFlag, 0xbb, StageGlobal::None, {45, -1, -1, -1}}, // 44
    {LayoutOp::RngStep, 0x0, StageGlobal::None, {46, 50, 54, 58}}, // 45
    {LayoutOp::SetFlag, 0xb2, StageGlobal::None, {47, -1, -1, -1}}, // 46
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {48, -1, -1, -1}}, // 47
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {49, -1, -1, -1}}, // 48
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 49
    {LayoutOp::SetFlag, 0xb0, StageGlobal::None, {51, -1, -1, -1}}, // 50
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {52, -1, -1, -1}}, // 51
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {53, -1, -1, -1}}, // 52
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 53
    {LayoutOp::SetFlag, 0xb1, StageGlobal::None, {55, -1, -1, -1}}, // 54
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {56, -1, -1, -1}}, // 55
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {57, -1, -1, -1}}, // 56
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 57
    {LayoutOp::SetFlag, 0xb2, StageGlobal::None, {59, -1, -1, -1}}, // 58
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {60, -1, -1, -1}}, // 59
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {61, -1, -1, -1}}, // 60
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 61
    {LayoutOp::ClearFlag, 0xbb, StageGlobal::None, {63, -1, -1, -1}}, // 62
    {LayoutOp::RngStep, 0x0, StageGlobal::None, {64, 68, 72, 76}}, // 63
    {LayoutOp::SetFlag, 0xb2, StageGlobal::None, {65, -1, -1, -1}}, // 64
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {66, -1, -1, -1}}, // 65
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {67, -1, -1, -1}}, // 66
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 67
    {LayoutOp::SetFlag, 0xb0, StageGlobal::None, {69, -1, -1, -1}}, // 68
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {70, -1, -1, -1}}, // 69
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {71, -1, -1, -1}}, // 70
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 71
    {LayoutOp::SetFlag, 0xb1, StageGlobal::None, {73, -1, -1, -1}}, // 72
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {74, -1, -1, -1}}, // 73
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {75, -1, -1, -1}}, // 74
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 75
    {LayoutOp::SetFlag, 0xb2, StageGlobal::None, {77, -1, -1, -1}}, // 76
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {78, -1, -1, -1}}, // 77
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {79, -1, -1, -1}}, // 78
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 79
    {LayoutOp::ClearFlag, 0xbb, StageGlobal::None, {81, -1, -1, -1}}, // 80
    {LayoutOp::RngStep, 0x0, StageGlobal::None, {82, 86, 90, 94}}, // 81
    {LayoutOp::SetFlag, 0xb2, StageGlobal::None, {83, -1, -1, -1}}, // 82
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {84, -1, -1, -1}}, // 83
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {85, -1, -1, -1}}, // 84
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 85
    {LayoutOp::SetFlag, 0xb0, StageGlobal::None, {87, -1, -1, -1}}, // 86
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {88, -1, -1, -1}}, // 87
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {89, -1, -1, -1}}, // 88
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 89
    {LayoutOp::SetFlag, 0xb1, StageGlobal::None, {91, -1, -1, -1}}, // 90
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {92, -1, -1, -1}}, // 91
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {93, -1, -1, -1}}, // 92
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 93
    {LayoutOp::SetFlag, 0xb2, StageGlobal::None, {95, -1, -1, -1}}, // 94
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {96, -1, -1, -1}}, // 95
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {97, -1, -1, -1}}, // 96
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 97
    {LayoutOp::ClearFlag, 0xbb, StageGlobal::None, {99, -1, -1, -1}}, // 98
    {LayoutOp::RngStep, 0x0, StageGlobal::None, {100, 104, 108, 112}}, // 99
    {LayoutOp::SetFlag, 0xb2, StageGlobal::None, {101, -1, -1, -1}}, // 100
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {102, -1, -1, -1}}, // 101
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {103, -1, -1, -1}}, // 102
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 103
    {LayoutOp::SetFlag, 0xb0, StageGlobal::None, {105, -1, -1, -1}}, // 104
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {106, -1, -1, -1}}, // 105
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {107, -1, -1, -1}}, // 106
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 107
    {LayoutOp::SetFlag, 0xb1, StageGlobal::None, {109, -1, -1, -1}}, // 108
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {110, -1, -1, -1}}, // 109
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {111, -1, -1, -1}}, // 110
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 111
    {LayoutOp::SetFlag, 0xb2, StageGlobal::None, {113, -1, -1, -1}}, // 112
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {114, -1, -1, -1}}, // 113
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {115, -1, -1, -1}}, // 114
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 115
    {LayoutOp::ClearFlag, 0xb0, StageGlobal::None, {117, -1, -1, -1}}, // 116
    {LayoutOp::ClearFlag, 0xb1, StageGlobal::None, {118, -1, -1, -1}}, // 117
    {LayoutOp::ClearFlag, 0xb2, StageGlobal::None, {119, -1, -1, -1}}, // 118
    {LayoutOp::TestFlag, 0xb6, StageGlobal::None, {120, 138, -1, -1}}, // 119
    {LayoutOp::ClearFlag, 0xb8, StageGlobal::None, {121, -1, -1, -1}}, // 120
    {LayoutOp::RngStep, 0x0, StageGlobal::None, {122, 126, 130, 134}}, // 121
    {LayoutOp::SetFlag, 0xb2, StageGlobal::None, {123, -1, -1, -1}}, // 122
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {124, -1, -1, -1}}, // 123
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {125, -1, -1, -1}}, // 124
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 125
    {LayoutOp::SetFlag, 0xb0, StageGlobal::None, {127, -1, -1, -1}}, // 126
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {128, -1, -1, -1}}, // 127
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {129, -1, -1, -1}}, // 128
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 129
    {LayoutOp::SetFlag, 0xb1, StageGlobal::None, {131, -1, -1, -1}}, // 130
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {132, -1, -1, -1}}, // 131
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {133, -1, -1, -1}}, // 132
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 133
    {LayoutOp::SetFlag, 0xb2, StageGlobal::None, {135, -1, -1, -1}}, // 134
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {136, -1, -1, -1}}, // 135
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {137, -1, -1, -1}}, // 136
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 137
    {LayoutOp::RngStep, 0x0, StageGlobal::None, {139, 157, 175, 193}}, // 138
    {LayoutOp::SetFlag, 0xb8, StageGlobal::None, {140, -1, -1, -1}}, // 139
    {LayoutOp::RngStep, 0x0, StageGlobal::None, {141, 145, 149, 153}}, // 140
    {LayoutOp::SetFlag, 0xb2, StageGlobal::None, {142, -1, -1, -1}}, // 141
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {143, -1, -1, -1}}, // 142
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {144, -1, -1, -1}}, // 143
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 144
    {LayoutOp::SetFlag, 0xb0, StageGlobal::None, {146, -1, -1, -1}}, // 145
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {147, -1, -1, -1}}, // 146
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {148, -1, -1, -1}}, // 147
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 148
    {LayoutOp::SetFlag, 0xb1, StageGlobal::None, {150, -1, -1, -1}}, // 149
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {151, -1, -1, -1}}, // 150
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {152, -1, -1, -1}}, // 151
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 152
    {LayoutOp::SetFlag, 0xb2, StageGlobal::None, {154, -1, -1, -1}}, // 153
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {155, -1, -1, -1}}, // 154
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {156, -1, -1, -1}}, // 155
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 156
    {LayoutOp::ClearFlag, 0xb8, StageGlobal::None, {158, -1, -1, -1}}, // 157
    {LayoutOp::RngStep, 0x0, StageGlobal::None, {159, 163, 167, 171}}, // 158
    {LayoutOp::SetFlag, 0xb2, StageGlobal::None, {160, -1, -1, -1}}, // 159
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {161, -1, -1, -1}}, // 160
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {162, -1, -1, -1}}, // 161
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 162
    {LayoutOp::SetFlag, 0xb0, StageGlobal::None, {164, -1, -1, -1}}, // 163
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {165, -1, -1, -1}}, // 164
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {166, -1, -1, -1}}, // 165
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 166
    {LayoutOp::SetFlag, 0xb1, StageGlobal::None, {168, -1, -1, -1}}, // 167
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {169, -1, -1, -1}}, // 168
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {170, -1, -1, -1}}, // 169
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 170
    {LayoutOp::SetFlag, 0xb2, StageGlobal::None, {172, -1, -1, -1}}, // 171
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {173, -1, -1, -1}}, // 172
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {174, -1, -1, -1}}, // 173
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 174
    {LayoutOp::ClearFlag, 0xb8, StageGlobal::None, {176, -1, -1, -1}}, // 175
    {LayoutOp::RngStep, 0x0, StageGlobal::None, {177, 181, 185, 189}}, // 176
    {LayoutOp::SetFlag, 0xb2, StageGlobal::None, {178, -1, -1, -1}}, // 177
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {179, -1, -1, -1}}, // 178
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {180, -1, -1, -1}}, // 179
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 180
    {LayoutOp::SetFlag, 0xb0, StageGlobal::None, {182, -1, -1, -1}}, // 181
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {183, -1, -1, -1}}, // 182
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {184, -1, -1, -1}}, // 183
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 184
    {LayoutOp::SetFlag, 0xb1, StageGlobal::None, {186, -1, -1, -1}}, // 185
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {187, -1, -1, -1}}, // 186
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {188, -1, -1, -1}}, // 187
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 188
    {LayoutOp::SetFlag, 0xb2, StageGlobal::None, {190, -1, -1, -1}}, // 189
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {191, -1, -1, -1}}, // 190
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {192, -1, -1, -1}}, // 191
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 192
    {LayoutOp::ClearFlag, 0xb8, StageGlobal::None, {194, -1, -1, -1}}, // 193
    {LayoutOp::RngStep, 0x0, StageGlobal::None, {195, 199, 203, 207}}, // 194
    {LayoutOp::SetFlag, 0xb2, StageGlobal::None, {196, -1, -1, -1}}, // 195
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {197, -1, -1, -1}}, // 196
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {198, -1, -1, -1}}, // 197
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 198
    {LayoutOp::SetFlag, 0xb0, StageGlobal::None, {200, -1, -1, -1}}, // 199
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {201, -1, -1, -1}}, // 200
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {202, -1, -1, -1}}, // 201
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 202
    {LayoutOp::SetFlag, 0xb1, StageGlobal::None, {204, -1, -1, -1}}, // 203
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {205, -1, -1, -1}}, // 204
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {206, -1, -1, -1}}, // 205
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 206
    {LayoutOp::SetFlag, 0xb2, StageGlobal::None, {208, -1, -1, -1}}, // 207
    {LayoutOp::Write, 0x0, StageGlobal::EncounterZoneTables, {209, -1, -1, -1}}, // 208
    {LayoutOp::Write, 0x0, StageGlobal::EncounterGroupTables, {210, -1, -1, -1}}, // 209
    {LayoutOp::End, 0x0, StageGlobal::None, {-1, -1, -1, -1}}, // 210
};
static constexpr EncounterRest kEncStageRest[] = {
    {0x4d, EncounterRestKind::Write, StageGlobal::AnimatedClasses, 0x0, 0},
    {0x4d, EncounterRestKind::Write, StageGlobal::StaticClasses, 0x0, 0},
    {0x4d, EncounterRestKind::Write, StageGlobal::ProjectileClasses, 0x0, 0},
    {0x4d, EncounterRestKind::Write, StageGlobal::EffectClasses, 0x0, 0},
    {0x4d, EncounterRestKind::Write, StageGlobal::ControllerClasses, 0x0, 0},
    {0x4d, EncounterRestKind::Write, StageGlobal::EncounterFieldTable, 0x0, 0},
    {0x4d, EncounterRestKind::Write, StageGlobal::EncounterEnemyGroups, 0x0, 0},
    {0x4d, EncounterRestKind::Write, StageGlobal::ClutAnimations, 0x0, 0},
    {0x4d, EncounterRestKind::Write, StageGlobal::GroundKinds, 0x0, 0},
    {0x4d, EncounterRestKind::Call, StageGlobal::None, 0x11, 0x0, RoutineId::EncounterSlotsSetup},
    {0x4d, EncounterRestKind::Write, StageGlobal::TransitionHandlers, 0x0, 0},
    {0x4d, EncounterRestKind::Write, StageGlobal::StageVoices, 0x0, 0},
    {0x4d, EncounterRestKind::Write, StageGlobal::SecondVoices, 0x0, 0},
    {0x4d, EncounterRestKind::Call, StageGlobal::None, 0x41, 0x1, RoutineId::StageMusicCall},
    {0x4d, EncounterRestKind::Write, StageGlobal::SecondRoomCallback, 0x0, 0, RoutineId::St4ASecondRoomCallback},
    {0x4d, EncounterRestKind::Write, StageGlobal::RoomCallback, 0x0, 0, RoutineId::St4ARoomCallback},
    {0x4e, EncounterRestKind::Write, StageGlobal::AnimatedClasses, 0x0, 0},
    {0x4e, EncounterRestKind::Write, StageGlobal::StaticClasses, 0x0, 0},
    {0x4e, EncounterRestKind::Write, StageGlobal::ProjectileClasses, 0x0, 0},
    {0x4e, EncounterRestKind::Write, StageGlobal::EffectClasses, 0x0, 0},
    {0x4e, EncounterRestKind::Write, StageGlobal::ControllerClasses, 0x0, 0},
    {0x4e, EncounterRestKind::Write, StageGlobal::EncounterFieldTable, 0x0, 0},
    {0x4e, EncounterRestKind::Write, StageGlobal::EncounterEnemyGroups, 0x0, 0},
    {0x4e, EncounterRestKind::Write, StageGlobal::ClutAnimations, 0x0, 0},
    {0x4e, EncounterRestKind::Write, StageGlobal::GroundKinds, 0x0, 0},
    {0x4e, EncounterRestKind::Call, StageGlobal::None, 0x15, 0x0, RoutineId::EncounterSlotsSetup},
    {0x4e, EncounterRestKind::Write, StageGlobal::TransitionHandlers, 0x0, 0},
    {0x4e, EncounterRestKind::Write, StageGlobal::StageVoices, 0x0, 0},
    {0x4e, EncounterRestKind::Write, StageGlobal::SecondVoices, 0x0, 0},
    {0x4e, EncounterRestKind::Call, StageGlobal::None, 0x42, 0x1, RoutineId::StageMusicCall},
    {0x4e, EncounterRestKind::Write, StageGlobal::SecondRoomCallback, 0x0, 0, RoutineId::St51SecondRoomCallback},
    {0x4e, EncounterRestKind::Write, StageGlobal::RoomCallback, 0x0, 0, RoutineId::St51RoomCallback},
    {0x4f, EncounterRestKind::Write, StageGlobal::AnimatedClasses, 0x0, 0},
    {0x4f, EncounterRestKind::Write, StageGlobal::StaticClasses, 0x0, 0},
    {0x4f, EncounterRestKind::Write, StageGlobal::ProjectileClasses, 0x0, 0},
    {0x4f, EncounterRestKind::Write, StageGlobal::EffectClasses, 0x0, 0},
    {0x4f, EncounterRestKind::Write, StageGlobal::ControllerClasses, 0x0, 0},
    {0x4f, EncounterRestKind::Write, StageGlobal::EncounterFieldTable, 0x0, 0},
    {0x4f, EncounterRestKind::Write, StageGlobal::EncounterEnemyGroups, 0x0, 0},
    {0x4f, EncounterRestKind::Write, StageGlobal::ClutAnimations, 0x0, 0},
    {0x4f, EncounterRestKind::Write, StageGlobal::GroundKinds, 0x0, 0},
    {0x4f, EncounterRestKind::Call, StageGlobal::None, 0x15, 0x0, RoutineId::EncounterSlotsSetup},
    {0x4f, EncounterRestKind::Write, StageGlobal::TransitionHandlers, 0x0, 0},
    {0x4f, EncounterRestKind::Write, StageGlobal::StageVoices, 0x0, 0},
    {0x4f, EncounterRestKind::Write, StageGlobal::SecondVoices, 0x0, 0},
    {0x4f, EncounterRestKind::Call, StageGlobal::None, 0x43, 0x1, RoutineId::StageMusicCall},
    {0x4f, EncounterRestKind::Write, StageGlobal::SecondRoomCallback, 0x0, 0, RoutineId::St4FSecondRoomCallback},
    {0x4f, EncounterRestKind::Write, StageGlobal::RoomCallback, 0x0, 0, RoutineId::St4FRoomCallback},
    {0x50, EncounterRestKind::Write, StageGlobal::AnimatedClasses, 0x0, 0},
    {0x50, EncounterRestKind::Write, StageGlobal::StaticClasses, 0x0, 0},
    {0x50, EncounterRestKind::Write, StageGlobal::ProjectileClasses, 0x0, 0},
    {0x50, EncounterRestKind::Write, StageGlobal::EffectClasses, 0x0, 0},
    {0x50, EncounterRestKind::Write, StageGlobal::ControllerClasses, 0x0, 0},
    {0x50, EncounterRestKind::Write, StageGlobal::EncounterFieldTable, 0x0, 0},
    {0x50, EncounterRestKind::Write, StageGlobal::EncounterEnemyGroups, 0x0, 0},
    {0x50, EncounterRestKind::Write, StageGlobal::ClutAnimations, 0x0, 0},
    {0x50, EncounterRestKind::Write, StageGlobal::GroundKinds, 0x0, 0},
    {0x50, EncounterRestKind::Call, StageGlobal::None, 0x15, 0x0, RoutineId::EncounterSlotsSetup},
    {0x50, EncounterRestKind::Write, StageGlobal::TransitionHandlers, 0x0, 0},
    {0x50, EncounterRestKind::Write, StageGlobal::StageVoices, 0x0, 0},
    {0x50, EncounterRestKind::Write, StageGlobal::SecondVoices, 0x0, 0},
    {0x50, EncounterRestKind::Call, StageGlobal::None, 0x44, 0x41, RoutineId::StageMusicCall},
    {0x50, EncounterRestKind::Call, StageGlobal::None, 0x2, 0x0, RoutineId::EmptyRoomHandler},
    {0x50, EncounterRestKind::Write, StageGlobal::SecondRoomCallback, 0x0, 0, RoutineId::St4ASecondRoomCallback},
    {0x50, EncounterRestKind::Write, StageGlobal::RoomCallback, 0x0, 0, RoutineId::St4ARoomCallback},
};
// ---- coverage (a summary of the exe's tables, counts only; groups can also be spawned by room scripts:
// exe 0x5e6640 ..) ----
// ST00 bits 0x1
//   subs 3, zone records 66 (66 trigger), groups 1 (0 records), groups referenced by zones [0]
//   classes (pool/type.sub x count): 
// ST0D bit 1 (random field encounters)
// ST0D bits 0x12 (no zone-triggered / script groups)
// ST0F bits 0x1
//   subs 13, zone records 12 (12 trigger), groups 9 (26 records), groups referenced by zones [0, 1, 2, 3, 4, 5, 6]
//   classes (pool/type.sub x count): 20/05.00x18 20/08.00x8
// ST10 bits 0x15
//   subs 2, zone records 0 (0 trigger), groups 2 (16 records), groups referenced by zones []
//   classes (pool/type.sub x count): 20/17.00x8 20/29.00x8
// ST11 bits 0x10 (no zone-triggered / script groups)
// ST12 bits 0x5
//   subs 7, zone records 5 (5 trigger), groups 8 (38 records), groups referenced by zones [1, 7]
//   classes (pool/type.sub x count): 20/03.00x3 20/04.00x6 20/22.00x4 20/2a.00x13 20/31.00x9 60/17.00x3
// ST13 bits 0x5
//   subs 11, zone records 8 (8 trigger), groups 10 (37 records), groups referenced by zones [2, 5, 8]
//   classes (pool/type.sub x count): 20/04.00x4 20/19.00x9 20/22.00x9 20/31.01x8 60/17.00x6 a1/02.00x1
// ST14 bits 0x5
//   subs 5, zone records 4 (4 trigger), groups 4 (11 records), groups referenced by zones [0, 2]
//   classes (pool/type.sub x count): 20/03.00x2 20/04.00x4 20/15.01x3 20/31.00x2
// ST15 bits 0x14
//   subs 1, zone records 1 (1 trigger), groups 1 (0 records), groups referenced by zones [0]
//   classes (pool/type.sub x count): 
// ST16 bits 0x14
//   subs 1, zone records 1 (1 trigger), groups 1 (0 records), groups referenced by zones [0]
//   classes (pool/type.sub x count): 
// ST17 bits 0x14
//   subs 3, zone records 3 (3 trigger), groups 1 (0 records), groups referenced by zones [0]
//   classes (pool/type.sub x count): 
// ST18 bits 0x4
//   subs 6, zone records 6 (6 trigger), groups 1 (0 records), groups referenced by zones [0]
//   classes (pool/type.sub x count): 
// ST1D bits 0x10 (no zone-triggered / script groups)
// ST1F bits 0x14
//   subs 2, zone records 0 (0 trigger), groups 0 (0 records), groups referenced by zones []
//   classes (pool/type.sub x count): 
// ST22 bit 1 (random field encounters)
// ST22 bits 0x12 (no zone-triggered / script groups)
// ST26 bits 0x15
//   subs 7, zone records 0 (0 trigger), groups 11 (54 records), groups referenced by zones []
//   classes (pool/type.sub x count): 20/04.00x4 20/29.01x14 20/3b.01x6 20/61.00x14 20/64.00x2 60/04.00x14
// ST27 bits 0x15
//   subs 9, zone records 0 (0 trigger), groups 5 (19 records), groups referenced by zones []
//   classes (pool/type.sub x count): 20/35.00x8 20/61.00x1 20/67.00x2 20/67.01x8
// ST28 bit 1 (random field encounters)
// ST28 bits 0x17
//   subs 6, zone records 0 (0 trigger), groups 1 (2 records), groups referenced by zones []
//   classes (pool/type.sub x count): 20/6c.00x2
// ST29 bits 0x4
//   subs 2, zone records 2 (2 trigger), groups 1 (4 records), groups referenced by zones [0]
//   classes (pool/type.sub x count): 20/0d.00x1 20/0f.00x3
// ST2A bits 0x4
//   subs 5, zone records 5 (5 trigger), groups 2 (7 records), groups referenced by zones [0]
//   classes (pool/type.sub x count): 20/0b.00x1 20/0b.01x3 60/0c.00x3
// ST2B bits 0x5
//   subs 4, zone records 0 (0 trigger), groups 3 (13 records), groups referenced by zones []
//   classes (pool/type.sub x count): 20/0b.00x3 20/0f.00x3 20/14.00x1 60/01.00x6
// ST2D bits 0x14
//   subs 10, zone records 10 (10 trigger), groups 7 (23 records), groups referenced by zones [0]
//   classes (pool/type.sub x count): 20/04.00x6 20/05.01x6 20/34.00x2 20/34.04x1 20/36.00x2 20/4f.00x2 20/52.00x1 20/56.00x1 60/30.00x2
// ST2E bits 0x4
//   subs 12, zone records 12 (12 trigger), groups 0 (0 records), groups referenced by zones [0]
//   classes (pool/type.sub x count): 
// ST2F bit 1 (random field encounters)
// ST2F bits 0x16
//   subs 9, zone records 9 (9 trigger), groups 1 (2 records), groups referenced by zones [0]
//   classes (pool/type.sub x count): 20/17.00x2
// ST30 bits 0x14
//   subs 5, zone records 5 (5 trigger), groups 6 (48 records), groups referenced by zones [0]
//   classes (pool/type.sub x count): 20/2d.01x8 20/2d.03x7 20/40.01x5 20/41.00x2 20/57.00x17 60/1b.01x1 60/1b.03x8
// ST31 bits 0x10 (no zone-triggered / script groups)
// ST33 bits 0x11
//   subs 1, zone records 0 (0 trigger), groups 1 (0 records), groups referenced by zones []
//   classes (pool/type.sub x count): 
// ST34 bits 0x15
//   subs 6, zone records 0 (0 trigger), groups 8 (38 records), groups referenced by zones []
//   classes (pool/type.sub x count): 20/04.00x6 20/15.01x1 20/35.00x8 20/61.00x6 20/64.00x6 20/67.00x3 20/67.01x8
// ST36 bits 0x5
//   subs 14, zone records 0 (0 trigger), groups 10 (34 records), groups referenced by zones []
//   classes (pool/type.sub x count): 20/04.00x8 20/19.00x8 20/2a.00x5 60/04.00x13
// ST37 bits 0x5
//   subs 16, zone records 0 (0 trigger), groups 12 (64 records), groups referenced by zones []
//   classes (pool/type.sub x count): 20/19.00x11 20/76.00x3 20/77.00x4 20/77.01x46
// ST38 bits 0x1
//   subs 14, zone records 0 (0 trigger), groups 10 (37 records), groups referenced by zones []
//   classes (pool/type.sub x count): 20/04.00x6 20/77.01x15 60/04.00x16
// ST3B bits 0x5
//   subs 2, zone records 0 (0 trigger), groups 3 (32 records), groups referenced by zones []
//   classes (pool/type.sub x count): 20/0d.00x3 20/12.00x3 60/01.00x4 e0/0a.00x22
// ST41 bits 0x5
//   subs 15, zone records 6 (6 trigger), groups 14 (14 records), groups referenced by zones [8, 9, 10, 11, 12, 13]
//   classes (pool/type.sub x count): 20/05.00x2 20/08.00x2 20/10.00x1 20/19.00x3 20/7c.00x6
// ST42 bits 0x5
//   subs 16, zone records 3 (3 trigger), groups 5 (15 records), groups referenced by zones [0, 4]
//   classes (pool/type.sub x count): 20/05.00x6 20/10.00x5 20/7c.00x4
// ST44 bits 0x4
//   subs 5, zone records 5 (5 trigger), groups 1 (3 records), groups referenced by zones [0]
//   classes (pool/type.sub x count): 20/76.00x3
// ST45 bits 0x4
//   subs 6, zone records 6 (6 trigger), groups 1 (1 records), groups referenced by zones [0]
//   classes (pool/type.sub x count): 20/45.00x1
// ST4A bits 0x11
//   subs 7, zone records 6 (6 trigger), groups 4 (11 records), groups referenced by zones [0, 1, 2, 3]
//   classes (pool/type.sub x count): 20/05.00x7 60/25.00x4
// ST4D bits 0x11 (RNG layouts: 3 zone tables)
//   group table 0: subs 9, zone records 17 (17 trigger), groups 11 (33 records), groups referenced by zones [0, 1, 2, 3, 4, 5, 6, 7, 8]
//   group table 0: classes (pool/type.sub x count): 20/04.00x4 20/08.00x3 20/15.01x3 20/19.00x5 20/2a.00x14 60/04.00x2 e0/13.00x2
//   group table 1: subs 9, zone records 17 (17 trigger), groups 7 (30 records), groups referenced by zones [0, 1, 2, 3, 4, 5, 6, 7, 8]
//   group table 1: classes (pool/type.sub x count): 20/04.00x3 20/08.00x4 20/15.01x2 20/19.00x12 20/2a.00x5 60/04.00x3 60/45.00x1
//   group table 2: subs 9, zone records 17 (17 trigger), groups 8 (26 records), groups referenced by zones [0, 1, 2, 3, 4, 5, 6, 7, 8]
//   group table 2: classes (pool/type.sub x count): 20/04.00x3 20/08.00x1 20/15.01x1 20/19.00x5 20/2a.00x9 20/56.00x3 60/04.00x2 60/45.00x1 e0/13.00x1
// ST4E bits 0x15 (RNG layouts: 1 zone tables)
//   group table 0: subs 10, zone records 0 (0 trigger), groups 9 (33 records), groups referenced by zones []
//   group table 0: classes (pool/type.sub x count): 20/04.00x5 20/08.00x6 20/15.01x4 20/31.00x13 60/04.00x5
//   group table 1: subs 10, zone records 0 (0 trigger), groups 9 (37 records), groups referenced by zones []
//   group table 1: classes (pool/type.sub x count): 20/04.00x8 20/08.00x7 20/15.01x3 20/31.00x13 60/04.00x6
//   group table 2: subs 10, zone records 0 (0 trigger), groups 9 (32 records), groups referenced by zones []
//   group table 2: classes (pool/type.sub x count): 20/04.00x5 20/08.00x7 20/15.01x6 20/2a.00x6 20/31.00x5 60/04.00x3
// ST4F bits 0x15 (RNG layouts: 1 zone tables)
//   group table 0: subs 14, zone records 0 (0 trigger), groups 11 (20 records), groups referenced by zones []
//   group table 0: classes (pool/type.sub x count): 20/08.00x3 20/34.04x2 20/36.00x2 20/4f.00x4 20/53.00x1 60/04.00x8
//   group table 1: subs 14, zone records 0 (0 trigger), groups 11 (28 records), groups referenced by zones []
//   group table 1: classes (pool/type.sub x count): 20/08.00x1 20/34.04x3 20/36.00x2 20/4f.00x5 60/04.00x17
//   group table 2: subs 14, zone records 0 (0 trigger), groups 11 (26 records), groups referenced by zones []
//   group table 2: classes (pool/type.sub x count): 20/08.00x5 20/34.04x3 20/36.00x1 20/4f.00x2 60/04.00x15
// ST50 bits 0x15 (RNG layouts: 1 zone tables)
//   group table 0: subs 11, zone records 0 (0 trigger), groups 11 (35 records), groups referenced by zones []
//   group table 0: classes (pool/type.sub x count): 20/04.00x6 20/15.01x4 20/19.00x9 20/56.00x1 20/76.00x3 20/77.00x4 60/04.00x7 60/45.00x1
//   group table 1: subs 11, zone records 0 (0 trigger), groups 11 (33 records), groups referenced by zones []
//   group table 1: classes (pool/type.sub x count): 20/04.00x5 20/15.01x3 20/19.00x9 20/56.00x1 20/76.00x3 20/77.00x7 60/04.00x4 60/45.00x1
//   group table 2: subs 11, zone records 0 (0 trigger), groups 11 (33 records), groups referenced by zones []
//   group table 2: classes (pool/type.sub x count): 20/04.00x3 20/15.01x8 20/19.00x14 20/56.00x1 20/76.00x4 20/77.00x2 60/45.00x1
// ST51 bits 0x15
//   subs 3, zone records 3 (3 trigger), groups 3 (0 records), groups referenced by zones [0]
//   classes (pool/type.sub x count): 
// ST55 bits 0x14
//   subs 7, zone records 7 (7 trigger), groups 13 (54 records), groups referenced by zones [0]
//   classes (pool/type.sub x count): 20/04.00x8 20/19.00x8 20/2a.00x11 20/4f.00x5 20/53.00x3 60/04.00x17 e0/13.00x1 e0/21.00x1
