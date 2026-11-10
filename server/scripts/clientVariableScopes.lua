-- Place clientside variables in different categories to decide how they are synchronized,
-- saved and loaded
--
-- Note: Currently, only global variables are handled, not local or member variables
--
-- Descriptions:
-- * "ignored" is where you place variables that clients should not send packets about,
--   either because they are already handled in other packets or because they would cause
--   unnecessary packet spam
-- * "personal" is where you place variables that are always exclusive to specific players
--   and that should not be shared regardless of other server options
-- * "quest" is where you place variables that should be synchronized and shared across
--   players based on the value of config.shareJournal
-- * "kills" is where you place variables that should be handled the same as kill counts
--   and should be cleared whenever the regular kill counts are
-- * "factionRanks" is where you place variables that should be synchronized and shared across
--   players based on the value of config.shareFactionRanks
-- * "factionExpulsion" is where you place variables that should be synchronized and shared across
--   players based on the value of config.shareFactionExpulsion
-- * "worldwide" is where you place variables that are always shared across all players
--   because they affect the physical world in a way that should be visible to everyone,
--   i.e. they affect structures, mechanism states, water levels, and so on
local clientVariableScopes = {
    globals = {}
}

if tableHelper.containsCaseInsensitiveString(clientDataFiles, "Morrowind.esm") then

    local addedVariableScopes = {
        globals = {
            ignored = {
                -- game state
                "random100",
                -- game settings
                "npcvoicedistance",
                -- time
                "gamehour", "timescale", "month", "day", "year",
                -- player character details
                "pcrace",
                -- player state
                "chargenstate", "pchascrimegold", "crimegolddiscount", "crimegoldturnin", "pchasgolddiscount",
                "pchasturnin",
                -- player equipment
                "wearinglegionuni", "wearingordinatoruni", "wearinghelmhhda", "wraithguardequipped", "tgglove",
                -- quest variables that are already set correctly without being synced
                "fargothwalk",
                -- not actually used at all
                "abelmawiacounter"
            },
            personal = {
                -- player state
                "pcvampire",
                -- tavern rents
                "rent_pelagiad_halfway", "rent_smora_gateway", "rent_smora_faras", "rent_ebon_six", "rent_balmora_south",
                "rent_balmora_council", "rent_balmora_lucky", "rent_aldruhn_skar", "rent_telaruhn_plot", "rent_telbran_sethan",
                "rent_telmora_covenant", "rent_vos_varo", "rent_balmora_eight", "rent_caldera_shenk", "rent_maargan_andus",
                "rent_vivec_lizard", "rent_vivec_flower", "rent_vivec_black", "rent_ghost_dusk",
                -- miscellaneous variables related to player-specific actions
                "chargenbreadstate"
            },
            quest = {
                -- main quest
                "hortatorvotes", "heartdestroyed", "destroyblight",
                -- reading of confidential documents
                "unsealededryno1", "unsealedodral1",
                -- assassination quests
                -- Note: These are related to kill counts, but they make more sense being shared across
                --       players who share quests than being shared across players who share kills
                "mt_legitkills", "mt_newcrimelevel", "mt_writdiscount",
                -- side quests for rescues
                "freedslavescounter", "madurarescued",
                -- other side quests
                "threadswebspinner", "monopolyvotes", "bone", "ownershiphhcs"
            },
            kills = {
                -- main quest
                "redoranmurdered", "telvannidead",
                -- side quests
                "ratskilled", "vampkills"
            },
            factionRanks = {
                -- membership in mini-factions
                "vampclan"
            },
            factionExpulsion = {
                -- faction expulsion forgiveness and timers
                "expredoran", "expmagesguild", "expfightersguild", "exptemple", "expmoragtong", "expimperialcult",
                "expimperiallegion", "expthievesguild"
            },
            worldwide = {
                -- mechanisms
                "gg_gate1_state", "gg_gate2_state",
                -- building construction
                "stronghold",
                -- whether duel arenas are occupied
                "duelactive"
            },
            unknown = {
            }
        }
    }

    tableHelper.merge(clientVariableScopes, addedVariableScopes, true)
end

if tableHelper.containsCaseInsensitiveString(clientDataFiles, "Tribunal.esm") then

    local addedVariableScopes = {
        globals = {
            ignored = {
                -- player state
                "pcgold",
                -- quest variables that are already set correctly without being synced
                "mercenarynear"
            },
            personal = {
                -- tavern rent
                "rent_mh_guar",
                -- mercenary contracts
                "contractcalvusday", "contractcalvusmonth", "contract_calvus_days_left",
                -- fights started
                "kinghit"
            },
            quest = {
                -- main quest
                "dbattack", "mournholdattack", "fabattack", "shrinecleanse", "bladefix", "hasblade", "kgaveblade",
                "duelmiss", "karrodbribe", "karrodbeaten", "karrodfightstart", "karrodcheapshot",
                -- side quests
                "plaguerock", "plagueactivate", "plaguestage", "droth_var", "museumdonations", "matchmakeswitch",
                "matchmakefons", "matchmakegoval", "matchmakesunel"
            },
            kills = {
                -- main quest
                "gobchiefdead", "helsassdead"
            },
            worldwide = {
                -- weather overrides
                "mournweather"
            }
        }
    }

    tableHelper.merge(clientVariableScopes, addedVariableScopes, true)
end

if tableHelper.containsCaseInsensitiveString(clientDataFiles, "Bloodmoon.esm") then

    local addedVariableScopes = {
        globals = {
            ignored = {
                -- player state
                "pcwerewolf",
                -- quest variables that are already set correctly without being synced
                "trackerpause"
            },
            personal = {
                -- player state
                "pcknownreset",
                -- tavern rents
                "rent_ravenrock"
            },
            quest = {
                -- main quest
                "foundbooze", "artoriachosen", "luciuschosen", "stones", "part", "aesliptalk", "skaalattack", "trackercount",
                "huntercount", "cariustalk",
                -- Raven Rock
                "colonyside", "maryndetect", "colonystock"
            },
            kills = {
                -- main quest
                "smugdead", "riekkilled", "deaddaedra", "caenlorndead", "werewolfdead", "werebdead", "huntersdead",
                "trackersdead", "trollsdead", "krishcount",
                -- side quests
                "draugrkilled", "colonynord"
            },
            worldwide = {
                -- building construction
                "colonystate",
                -- NPCs present
                "colonyservice", "carniusloc", "aferguard", "garnasguard", "gratianguard"
            }
        }
    }

    tableHelper.merge(clientVariableScopes, addedVariableScopes, true)
end

if tableHelper.containsCaseInsensitiveString(clientDataFiles, "Tamriel_Data.ESM") then

    local addedVariableScopes = {
        globals = {
            ignored = {
                -- game state
                "TR_MapPos", "TR_CellX", "TR_CellY", "TR_Test", "PC_NoLore", "T_Glob_cleanup_x", "T_Glob_cleanup_y", 
                "T_Glob_cleanup_z", "T_Glob_cleanup_state", "T_Glob_DWelk_cleanup", "T_Glb_GetTeleportingDisabled", 
                "T_Glob_PassTimeHours", "T_Glob_GetTeleportingDisabled", "T_Glob_Speech_Debug", "T_Glob_Speech_Sway", 
                "T_Glob_Speech_Haggle", "T_Glob_Speech_Debate", 
                
                -- card game
                "T_Glob_CardHortX", "T_Glob_CardHortY",    "T_Glob_CardHortZ", "T_Glob_CardHortReshapeX", "T_Glob_CardHortReshapeY",
                "T_Glob_CardHortCol1Len", "T_Glob_CardHortCol2Len", "T_Glob_CardHortCol3Len", "T_Glob_CardHortCol4Len",
                "T_Glob_CardHortCol5Len", "T_Glob_CardHortCol6Len", "T_Glob_CardHortCol1Lock", "T_Glob_CardHortCol2Lock", 
                "T_Glob_CardHortCol3Lock", "T_Glob_CardHortCol4Lock", "T_Glob_CardHortCol5Lock", "T_Glob_CardHortCol6Lock",
                "T_Glob_CardHortCol1Lock2", "T_Glob_CardHortCol2Lock2", "T_Glob_CardHortCol3Lock2", "T_Glob_CardHortCol4Lock2", 
                "T_Glob_CardHortCol5Lock2", "T_Glob_CardHortCol6Lock2", "T_Glob_CardHortSaveLoad", "T_Glob_CardHortActiveLen",
                "T_Glob_CardHortTop", "T_Glob_CardHortDummy", "T_Glob_CardHortState", "T_Glob_CardHortTracker", "T_Glob_CardHortRow",
                "T_Glob_CardHortRot", "T_Glob_CardHortRank", "T_Glob_CardHortCol", "T_Glob_CardHortHouse"
                
            },
            personal = {
                -- player state
                "T_Glob_PorphyricInfected", "T_Glob_WereInfected",
                
                -- Bank accounts
                "T_Glob_Bank_All_CurrentBank", "T_Glob_Bank_Bri_AcctAmount", "T_Glob_Bank_Bri_LoanAmount",
                "T_Glob_Bank_Bri_LoanDate", "T_Glob_Bank_Bri_LoanFail", "T_Glob_Bank_Hla_LoanFail", "T_Glob_Bank_Hla_AcctAmount",
                "T_Glob_Bank_Hla_LoanAmount", "T_Glob_Bank_Hla_LoanDate",
                
            },
            quest = {
                -- reputation
                "T_Glob_Rep_Sky_Pr", "T_Glob_Rep_Sky_Re"
            },
            kills = {
            
            },
            factionRanks = {
                
            },
            factionExpulsion = {
                
            },
            worldwide = {
                -- mechanisms
                "T_Glob_SutchElevDir", "T_Glob_SutchElevRest", "T_Glob_SutchElevUpDownCounter",
                -- objects
                "T_Glob_KingOrgCoffer_Uses",
                -- news
                "T_Glob_News_Bellman_Pick1", "T_Glob_News_Bellman_Pick2", "T_Glob_News_Bellman_Tracker1", "T_Glob_News_Bellman_Tracker2"                
            },
            unknown = {
                
            }
        }
    }
    
    tableHelper.merge(clientVariableScopes, addedVariableScopes, true)
end

if tableHelper.containsCaseInsensitiveString(clientDataFiles, "TR_Mainland.ESM") then

    local addedVariableScopes = {
        globals = {
            ignored = {
                -- game state
                "TR_m3_q_Kha_BellTarget", "TR_m3_q_Kha_BellTracker", "TR_m3_OE_TarhielHaybale", "TR_m3_OE_customsfine",
                "TR_m3_OE_customsfine", "TR_m3_OE_customsnote", "TR_m3_OE_resettlement", "TR_m3_OE_esoldereward", "TR_m4_HH_ScribPie_Baker",
                -- player state
                "TR_m3_OE_armisticetheft", "TR_m3_OE_truearmistice", "tr_m3_bloodstone_help_T", "tr_m3_bloodstone_help_E", "tr_m3_bloodstone_help_T",
                "tr_m3_bloodstone_help_O", "tr_m3_bloodstone_help_B", "tr_m3_bloodstone_help_N", "tr_m3_bloodstone_help_L",
                -- player equipment
                "TR_m3_OE_MG_wearingrobe", "TR_m3_TT_FS_helm_on", "TR_m3_TT_InqMantle",
                -- quest variables that are already set correctly without being synced
                "TR_m3_bloodstonecheck"
            },
            personal = {
                -- player state
                "TR_m2_q_35_PCVampire",
                -- tavern rents
                "TR_m3_Rent_Bosmora_Starlight", "TR_m3_Rent_ED_Velk", "TR_m3_Rent_Gorne_EmeraldHaven",
                "TR_m2_Rent_Hlersis_Spore", "TR_m2_Rent_Akamora_Gob", "TR_m2_Rent_InnBetween", "TR_m2_Rent_Necrom_Hostel",
                "TR_m3_Rent_Meralag_Glade", "TR_m3_Rent_Darnim_Windbrk", "TR_Rent_HuntedHound", "TR_m1_Rent_Avenue",
                "TR_m2_Rent_Helnim_Drake", "TR_m2_Rent_Helnim_Flower", "TR_m2_Rent_Helnim_Racer", "TR_m2_Rent_Mothrivra_Goblet",
                "TR_m1_Rent_Black_Ogre", "TR_m1_Rent_Dancing_Jug", "TR_m1_Rent_Howling_Noose", "TR_m1_Rent_Queens_Cutlass", 
                "TR_m1_Rent_Waters_Shadow", "TR_m3_Rent_Sailen_Toiling", "TR_m3_Rent_Moth_and_Tiger", "TR_m3_Rent_Empress_Katariah", 
                "TR_m3_Rent_Salty_Futtocks", "TR_m3_Rent_Vhul_Hound", "TR_m3_Rent_Aimrah_Inn", "TR_m3_Rent_AT_HC", "TR_m3_Rent_AT_LS",
                "TR_m3_Rent_AT_TS",
                -- miscellaneous variables related to player-specific actions
                "TR_m3_Kha_Fountain_Cooldown", "TR_m3_Sa_PearlCurseTimer", "TR_m2_445_BlessingA", "TR_m2_445_BlessingS", "TR_m2_445_BlessingV"
            },
            quest = {
                -- main quest
                "TR_m2_445_KeyATracker", "TR_m2_445_KeySTracker", "TR_m2_445_KeyVTracker",
                -- reading of confidential documents
                "TR_m3_q_AT_recipe_sealed", "TR_m3_TT_IduraUnseal",
                -- assassination quests
                -- Note: These are related to kill counts, but they make more sense being shared across
                --       players who share quests than being shared across players who share kills
                "TR_m3_Zanammu_LichDead", "TR_m2_q_27_guardkilled", "TR_m2_q_35_dead", "TR_m3_q_4_dead", "TR_m3_OE_TG_AntioDead",
                "TR_m3_TT_CalitiaPreKilled", "TR_m3_TT_Lloris5IndCount", "TR_m4_VysAssaDead", "TR_m3_KH_kraskiradead",
                -- other side quests
                "TR_m3_q_3_thieving", "TR_Pilgrimages", "TR_m3_q_4_info", "TR_m3_q_3_info", "TR_m3_q_3_infoKiseen", "TR_m3_q_3_infoElegel", 
                "TR_m3_q_3_infoTemple", "TR_m3_q_3_infoFarys", "TR_m3_q_3_infoFarysWife", "TR_m2_q_38_sabotage", "TR_m2_q_38_talkedto", 
                "TR_m2_q_38_status", "TR_m2_q_A8_6_rushNPC", "TR_m3_q_3_rumour", "TR_m3_Bosvau_stolen", "TR_m3_Bosvau_stolenvalue", 
                "TR_m3_q_3_guardsGone", "TR_m2_MG_Aka_seeds", "TR_m2_MG_Aka_Francine1", "TR_m2_MG_Aka_karma", "TR_m2_MG_Aka_Polodie1reward",
                "TR_m2_MG_Aka_tarry", "TR_m3_q_5_Journal_Read", "TR_m3_q_5_distantJulie", "TR_m3_q_5_Journal_Read", "TR_m3_q_5_distantJulie", 
                "TR_m3_q_OE_MG_GCount", "TR_m3_OE_smuggleeggsfound", "TR_m3_OE_maesabunsale", "TR_m3_OE_FG_q_FledFromVermai", "TR_m3_OE_FG_q_AureCTalk", 
                "TR_m3_q_givebartsword", "TR_m3_q_fiendbladegot", "TR_m3_q_fienddisappear", "TR_m3_q_treasurebladestolen", "TR_m3_OE_RumaGlobal", "TR_m3_q_NelynFathisTimer",
                "TR_m3_AT_SilentNight_stage", "TR_m3_AT_SilentNight_Rat", "TR_m3_q_TheRiftDral", "TR_m3_q_TheRiftTilresi", "TR_m3_OE_MG_HallOpen", "TR_m3_Aim_ShipSneak", 
                "TR_m3_TT_ProverbCounter", "TR_m3_TT_Lloris4Indoril", "TR_m3_TT_Lloris4Hlaalu", "TR_m3_VysAssanudCheck", "TR_m3_TT_LatestRumorATGlobal", 
                "TR_m3_Kha_SY_convinced", "TR_m3_Kha_SY_final", "TR_m3_TT_RIP_garvs_heresy", "TR_m3_TT_RIP_refusecount", "TR_m4_TJ_Court_State", "TR_m2_NisirelConfronted",
                "TR_m3_q_A3_Seen_Basement", "TR_m3_OE_elysanadiamondstole", "TR_m3_OE_KtD_Tur", "TR_m3_OE_KtD_Gul", "TR_m3_OE_KtD_Mur", "TR_m3_OE_KtD_Ema",
                
            },
            kills = {

            },
            factionRanks = {
                
            },
            factionExpulsion = {
                -- faction expulsion forgiveness and timers
                "TR_Kick_FG", "TR_Kick_TG", "TR_Kick_TT", "TR_Kick_IC", "TR_Kick_MG"
            },
            worldwide = {
                -- mechanisms
                "TR_Necrom_StairsState", "TR_Necrom_MachineState", "TR_Necrom_VaultPortR", "TR_Necrom_VaultPortL",
                "TR_Necrom_DoorState", "TR_m2_445_grindertimer", "TR_m2_445_grinderangle1", "TR_m3_Aim_GilaWallBreak", 
                "TR_m3_Aim_LighthouseSecretDoor", "TR_m3_OE_pitgate", "TR_m3_OE_CuriaVaultGate2", "TR_m3_OE_sewergate", 
                "TR_m3_OE_MainGate", "TR_m3_OE_MainGMove", "TR_m2_kmlz_Chef_WaterLevel", "TR_m3_OE_ETCensusBlockDoor", 
                "TR_m3_OE_CuriaVaultGate", "TR_m3_OE_CuriaVaultGlobal", "TR_m3_OE_TG_waterlevel", "TR_m3_OE_chapelsewerdoor",
                "TR_m3_OE_raathim_sarcophagus", "TR_m3_Aim_GilaWallBreak", "TR_m3_q_OE_UrienChest_glb", "TR_Necrom_AllowVaultEntry",
                -- building construction
                "TR_m7_NVA_BuildStage", "TR_m3_OE_ETCensus_RepairState", "TR_m3_OE_ETCensus_Stanchion",
                -- objects
                "TR_m3_MaesabunMummyAwake", "TR_m3_AT_LatikaPitcher", "TR_m3_vontuswalk",
                -- people
                "TR_m2_WM_Rethrathi", "TR_m2_q_29_shambaludridrea", "TR_m4_TJ_OgrimStatus", "TR_m3_TT_Illene1_ChaseGlobal",
                -- both
                "TR_m3_OE_EECq1solve", "TR_m3_OE_EECq2solve",
                -- events
                "TR_m3_OE_StendarrIdolsOutlawed", "TR_Thirr_Conflict_Score", "TR_Thirr_Conflict_Heat", "TR_m3_TT_g_ritstart"
            },
            unknown = {
                
            }
        }
    }

    tableHelper.merge(clientVariableScopes, addedVariableScopes, true)
end

if tableHelper.containsCaseInsensitiveString(clientDataFiles, "Cyrodiil_Main.esm") then

    local addedVariableScopes = {
        globals = {
            ignored = {
                -- not actually used at all
                "PC_Q1_2_State", "PC_Q1_4_State", "PC_Q1_5_Travel"
            },
            personal = {
                -- tavern rents
                "PC_Rent_Stirk_Sloads_Tale", "PC_Rent_Stirk_Safe_Harbor"
            },
            quest = {
                
            },
            kills = {

            },
            factionRanks = {
                
            },
            factionExpulsion = {

            },
            worldwide = {
                -- mechanisms
                "PC_i1_51_Gate_State"
            },
            unknown = {
                
            }
        }
    }

    tableHelper.merge(clientVariableScopes, addedVariableScopes, true)
end

if tableHelper.containsCaseInsensitiveString(clientDataFiles, "Sky_Main.esm") then

    local addedVariableScopes = {
        globals = {
            ignored = {
                -- game state
                "Sky_qRe_KG1_global", "Sky_TempVar_glb",
                -- quest variables that are already set correctly without being synced
                "Sky_qRe_DSW04_BreadCounter_glb",
                -- not actually used at all
                "Sky_iRe_DH99_Wine_glb", "sky_qRe_KG4_AmbCount", "sky_qRe_KG4_Day",
                "sky_qRe_KG4_Day2"
            },
            personal = {
                -- player state
                "Sky_qRe_KG4_Transformed_glb",
                -- tavern rents
                "Sky_Rent_DSE_Shadowkey", "Sky_Rent_DSW_DragonFountain", "Sky_Rent_DSW_NukraTikil", 
                "Sky_Rent_HA_Jhorcian", "Sky_Rent_KW_Dancing_Saber", "Sky_Rent_KW_Ruby_Drake",
                "Sky_Rent_LH_Daracam", "Sky_Rent_MER_Rhuma", "Sky_Rent_VF_EvenOddsInn",
                -- mercenary contracts
                "Sky_Merc_Rismund_DaysLeft",
                -- miscellaneous variables related to player-specific actions
                "Sky_qRe_BM04_Door_glb", "Sky_qRe_KW01_Book1_glb", "Sky_qRe_KW01_Book2_glb", 
                "Sky_qRe_KW01_Book3_glb", "Sky_qRe_KW01_Book4_glb"
            },
            quest = {

                -- other side quests
                "Sky_qRe-Vornd1_GLOBAL1", "Sky_qRe-Vornd1_GLOBAL2", "Sky_qRe_DSE01_Donation_glb", 
                "Sky_qRe_DSTG03_Informants_glb", "Sky_qRe_DSTG07_MoveCael_glb", "Sky_qRe_DSW01_CacheFound_glb",
                "Sky_qRe_DSW01_Dagger_glb", "Sky_qRe_DSW01_Scimitar_glb", "Sky_qRe_DSW01_Saber_glb", 
                "Sky_qRe_DSW02_Auth_glb", "Sky_qRe_KG5_SViir_glb", "Sky_qRe_KWFG04_Owner_glb", 
                "Sky_qRE_KWTG07_Glb_QuestDone", "Sky_qRe_MAI04_Counter_glb", "Sky_qRe_NAR01_Investigate_glb"
                
            },
            kills = {
                -- main quest
                "Sky_qRe_DSMQ_AlaktolDead", "Sky_qRe_DSMQ_JonaDead",
                -- side quests
                "Sky_qRe_Ald1_global", "Sky_qRe_DS_B01_AlreadyDead_glb", "Sky_qRe_DS_B02_AlreadyDead_glb", 
                "Sky_qRe_DS_B05_Counter_glb", "Sky_qRe_DSW01_MesaraDead_glb", "Sky_qRe_BM_FhegainDead_glb",
                "Sky_qRe_HA1_glb_KillCheck", "Sky_qRe_HA1_RilorDead_glb", "Sky_qRe_KG2_Counter_glb",
                "Sky_qRe_KG4_Counter", "Sky_qRe_KW_B01_glb_Counter", "Sky_qRe_KW_B02_AlreadyDead",
                "Sky_qRe_KW_B04_glb_Counter", "Sky_qRe_KW_B06_glb_Counter", "Sky_qRe_KW_B08_glb_Counter",
                "Sky_qRe_KW_B09_glb_Counter", "Sky_qRe_KW_B10_AlreadyDead", "Sky_qRe_KW_B10_glb_Counter",
                "Sky_qRe_KW_MG06_GhostsKilled", "sky_qRe_KWFG02_Counter", "sky_qRe_KWFG03_Counter",
                "Sky_qRe_MAI03_Counter_glb", "Sky_qRe_MAI04_Dead_glb", "Sky_qRE_VF3_Killed_glb",
                -- arena kill counts
                "Sky_qRe_DSE04_Count02_glb", "Sky_qRe_DSE04_Count03_glb", "Sky_qRe_DSE04_Count05_glb", 
                "Sky_qRe_DSE04_Count07_glb"
            },
            factionRanks = {
                -- faction reputation
                "Sky_qRe_DSMG_Rep_Glb", "Sky_Rep_FireHand_glb",
                -- membership in mini-factions
                "Sky_qRe_DSE04_Owner_glb"
            },
            factionExpulsion = {
                -- faction expulsion forgiveness and timers
                "Sky_Glb_ExpFightersGuild", "Sky_qRe_KWTG_Expelled_glb"
            },
            worldwide = {
                -- mechanisms
                "Sky_iRe_KW_RPVault_glb", "Sky_qRe_KW_MG06_PenumbraState", "Sky_qRe_KWTG06_Button_glb",
                -- objects
                "Sky_qRe_DSW01_LetterState_glb",
                -- npc behavior
                "Sky_qRe_HA1_CultistState_glb", "Sky_qRe_HA3_Sick_glb", "Sky_qRe_KW_MG04_Returned_glb",
                -- arena State
                "Sky_qRe_DSE_ArenaFight_glb"
            },
            unknown = {
                
            }
        }
    }

    tableHelper.merge(clientVariableScopes, addedVariableScopes, true)
end

-- ArenaMP C21: generated synchronization coverage for Tamriel_Data.ESM
-- Source snapshot supplied with Data Files.zip. Every GLOB in this ESM that was not
-- already classified above is assigned a scope here. Volatile/debug/helper globals
-- are explicitly ignored instead of being synchronized.
if tableHelper.containsCaseInsensitiveString(clientDataFiles, "Tamriel_Data.ESM") then
    local c21VariableScopes = {
        globals = {
            ignored = {
                "MWSE_BUILD",
                "T_Glob_Installed_ABC",
                "T_Glob_Installed_Bkm",
                "T_Glob_Installed_Els",
                "T_Glob_Installed_Ham",
                "T_Glob_Installed_HR427",
                "T_Glob_Installed_PC",
                "T_Glob_Installed_PI",
                "T_Glob_Installed_SHotN",
                "T_Glob_Installed_Sum",
                "T_Glob_Installed_TR",
                "T_Glob_Installed_TRFM",
                "T_Glob_Installed_Val",
                "T_Glob_OpenMwLuaUsed",
                "T_Glob_TR_PreviewEnabled",
                "T_Glob_VanillaOverride",
            },
            personal = {
                "T_Glob_Bank_All_TempAmount",
                "T_Glob_Bank_Cmp_AcctAmount",
                "T_Glob_Bank_Cmp_LoanAmount",
                "T_Glob_Bank_Cmp_LoanDate",
                "T_Glob_Bank_Cmp_LoanFail",
                "T_Glob_Bank_Cro_AcctAmount",
                "T_Glob_Bank_Cro_LoanAmount",
                "T_Glob_Bank_Cro_LoanDate",
                "T_Glob_Bank_Cro_LoanFail",
                "T_Glob_Bank_Mas_AcctAmount",
                "T_Glob_Bank_Mas_LoanAmount",
                "T_Glob_Bank_Mas_LoanDate",
                "T_Glob_Bank_Mas_LoanFail",
                "T_Glob_JNS_BountyClear",
                "T_Glob_NineholesBet",
                "T_Glob_NineholesGameState",
                "T_Glob_NineholesNpcId",
                "T_Glob_NineholesOpponentId",
                "T_Glob_NineholesPlayerColor",
                "T_Glob_NineholesPlayerTurn",
                "T_Glob_NineholesPosTakenBy_01a",
                "T_Glob_NineholesPosTakenBy_01b",
                "T_Glob_NineholesPosTakenBy_01c",
                "T_Glob_NineholesPosTakenBy_02a",
                "T_Glob_NineholesPosTakenBy_02b",
                "T_Glob_NineholesPosTakenBy_02c",
                "T_Glob_NineholesPosTakenBy_03a",
                "T_Glob_NineholesPosTakenBy_03b",
                "T_Glob_NineholesPosTakenBy_03c",
                "T_Glob_NineholesPracticeMode",
                "T_Glob_NineholesSelectedPin",
                "T_Glob_PlesioHatchPC",
                "T_Glob_Rep_Cyr",
                "T_Glob_Rep_Ham",
                "T_Glob_Rep_HR",
                "T_Glob_Rep_MW",
                "T_Glob_Rep_PI",
                "T_Glob_Rep_Sky",
                "T_Glob_StockBaseAMC",
                "T_Glob_StockBaseATC",
                "T_Glob_StockBaseBC",
                "T_Glob_StockBaseBIC",
                "T_Glob_StockBaseBRC",
                "T_Glob_StockBaseCMC",
                "T_Glob_StockBaseCWA",
                "T_Glob_StockBaseEEC",
                "T_Glob_StockBaseFFRC",
                "T_Glob_StockBaseNWT",
                "T_Glob_StockBaseRAC",
                "T_Glob_StockBaseRHC",
                "T_Glob_StockBaseSCTC",
                "T_Glob_StockBaseSSC",
                "T_Glob_StockBaseSTC",
                "T_Glob_StockBaseUMC",
                "T_Glob_StockBaseWPG",
                "T_Glob_StockBaseWSC",
                "T_Glob_StockBaseZBC",
                "T_Glob_StockCompareAMC",
                "T_Glob_StockCompareATC",
                "T_Glob_StockCompareBC",
                "T_Glob_StockCompareBIC",
                "T_Glob_StockCompareBRC",
                "T_Glob_StockCompareCMC",
                "T_Glob_StockCompareCWA",
                "T_Glob_StockCompareEEC",
                "T_Glob_StockCompareFFRC",
                "T_Glob_StockCompareNWT",
                "T_Glob_StockCompareRAC",
                "T_Glob_StockCompareRHC",
                "T_Glob_StockCompareSCTC",
                "T_Glob_StockCompareSSC",
                "T_Glob_StockCompareSTC",
                "T_Glob_StockCompareUMC",
                "T_Glob_StockCompareWPG",
                "T_Glob_StockCompareWSC",
                "T_Glob_StockCompareZBC",
                "T_Glob_StockCountPlayerAMC",
                "T_Glob_StockCountPlayerATC",
                "T_Glob_StockCountPlayerBC",
                "T_Glob_StockCountPlayerBIC",
                "T_Glob_StockCountPlayerBRC",
                "T_Glob_StockCountPlayerCMC",
                "T_Glob_StockCountPlayerCWA",
                "T_Glob_StockCountPlayerEEC",
                "T_Glob_StockCountPlayerFFRC",
                "T_Glob_StockCountPlayerNWT",
                "T_Glob_StockCountPlayerRAC",
                "T_Glob_StockCountPlayerRHC",
                "T_Glob_StockCountPlayerSCTC",
                "T_Glob_StockCountPlayerSSC",
                "T_Glob_StockCountPlayerSTC",
                "T_Glob_StockCountPlayerUMC",
                "T_Glob_StockCountPlayerWPG",
                "T_Glob_StockCountPlayerWSC",
                "T_Glob_StockCountPlayerZBC",
                "T_Glob_StockEnableAMC",
                "T_Glob_StockEnableATC",
                "T_Glob_StockEnableBC",
                "T_Glob_StockEnableBIC",
                "T_Glob_StockEnableBRC",
                "T_Glob_StockEnableCMC",
                "T_Glob_StockEnableCWA",
                "T_Glob_StockEnableEEC",
                "T_Glob_StockEnableFFRC",
                "T_Glob_StockEnableNWT",
                "T_Glob_StockEnableRAC",
                "T_Glob_StockEnableRHC",
                "T_Glob_StockEnableSCTC",
                "T_Glob_StockEnableSSC",
                "T_Glob_StockEnableSTC",
                "T_Glob_StockEnableUMC",
                "T_Glob_StockEnableWPG",
                "T_Glob_StockEnableWSC",
                "T_Glob_StockEnableZBC",
                "T_Glob_StockPayout001",
                "T_Glob_StockPayout005",
                "T_Glob_StockPayout010",
                "T_Glob_StockPayout050",
                "T_Glob_StockPayout100",
                "T_Glob_StockPayoutAll",
                "T_Glob_StockPriceAMC",
                "T_Glob_StockPriceATC",
                "T_Glob_StockPriceBC",
                "T_Glob_StockPriceBIC",
                "T_Glob_StockPriceBRC",
                "T_Glob_StockPriceCMC",
                "T_Glob_StockPriceCWA",
                "T_Glob_StockPriceEEC",
                "T_Glob_StockPriceFFRC",
                "T_Glob_StockPriceNWT",
                "T_Glob_StockPriceRAC",
                "T_Glob_StockPriceRHC",
                "T_Glob_StockPriceSCTC",
                "T_Glob_StockPriceSSC",
                "T_Glob_StockPriceSTC",
                "T_Glob_StockPriceUMC",
                "T_Glob_StockPriceWPG",
                "T_Glob_StockPriceWSC",
                "T_Glob_StockPriceZBC",
                "T_Glob_StockRequest",
                "T_Glob_StockSellCount",
                "T_Glob_StockSellPrice",
                "T_Glob_StockStarted",
                "T_Glob_StockValueTradedToday",
            },
            quest = {
                "T_Glob_VampDamageRemove",
            },
            kills = {
            },
            factionRanks = {

            },
            factionExpulsion = {
                "T_Glob_Exp_Cyr_FG",
                "T_Glob_Exp_Cyr_TG",
                "T_Glob_Exp_Itin_Priests",
                "T_Glob_Exp_King_Anv",
                "T_Glob_Exp_Sky_FG",
                "T_Glob_Exp_Sky_TG",
                "T_Glob_Exp_Tr_HI",
                "T_Glob_Exp_Tr_Ord",
            },
            worldwide = {
                "T_Glob_DaeWardAState",
                "T_Glob_DaeWardBState",
                "T_Glob_DaeWardCState",
                "T_Glob_DaeWardDState",
                "T_Glob_DaeWardEState",
                "T_Glob_DaeWardFState",
                "T_Glob_DaeWardGState",
                "T_Glob_DaeWardHState",
                "T_Glob_DaeWardIState",
                "T_Glob_DaeWardJState",
                "T_Glob_DaeWardKState",
                "T_Glob_DaeWardLState",
                "T_Glob_DaeWardMState",
                "T_Glob_DaeWardNState",
                "T_Glob_DaeWardOState",
                "T_Glob_DaeWardPState",
                "T_Glob_DaeWardQState",
                "T_Glob_DaeWardRState",
                "T_Glob_DaeWardSState",
                "T_Glob_DaeWardTState",
                "T_Glob_DaeWardUState",
                "T_Glob_DaeWardVState",
                "T_Glob_DaeWardWState",
                "T_Glob_DaeWardZState",
                "T_Glob_KingOrgCoffer_Gold",
                "T_Glob_News_AbMonitor_Pick1",
                "T_Glob_News_AbMonitor_Pick2",
                "T_Glob_News_AbMonitor_Tracker1",
                "T_Glob_News_AbMonitor_Tracker2",
                "T_Glob_News_Compass_Pick1",
                "T_Glob_News_Compass_Pick2",
                "T_Glob_News_Compass_Tracker1",
                "T_Glob_News_Compass_Tracker2",
                "T_Glob_News_Echo_Pick1",
                "T_Glob_News_Echo_Pick2",
                "T_Glob_News_Echo_Tracker1",
                "T_Glob_News_Echo_Tracker2",
            }
        }
    }
    tableHelper.merge(clientVariableScopes, c21VariableScopes, true)
end


-- ArenaMP C21: generated synchronization coverage for TR_Mainland.ESM
-- Source snapshot supplied with Data Files.zip. Every GLOB in this ESM that was not
-- already classified above is assigned a scope here. Volatile/debug/helper globals
-- are explicitly ignored instead of being synchronized.
if tableHelper.containsCaseInsensitiveString(clientDataFiles, "TR_Mainland.ESM") then
    local c21VariableScopes = {
        globals = {
            ignored = {
                "TR_m1_FWMG_SupportLua",
                "TR_m1_HT_MithrasDead",
                "TR_m2_IC_HauntingShield",
                "TR_m2_q_TheRiftSqLevel",
                "TR_m3_MG_OE_Q5_DaedraSummoned",
                "TR_m4_AndasGuestState",
                "TR_m4_baluathdead",
                "TR_m4_Barendreth_DeadOrd",
                "TR_m4_q_TTSCombatState",
                "TR_m7_AI_JNS_5_SmugglerDead",
                "TR_m7_ErnasiViranDead",
                "TR_m7_GauntletPoorL_Taken_glb",
                "TR_m7_GauntletPoorR_Taken_Glb",
                "TR_m7_HOSewerAlert",
                "TR_m7_Ida_NetMoved",
                "TR_m7_LongJump_var",
                "TR_m7_Ns_MG_Ench_Glb",
                "TR_m7_Ns_TG_8_GuardState",
                "TR_m7_Ns_TG_8_GuardWarn",
                "TR_m7_Ns_TG_9_GuardState",
                "TR_m7_Ns_TG_9_GuardWarn",
                "TR_m7_q_CE_CanLeashWorkers",
                "TR_m7_Rent_Narsis_Temple",
            },
            personal = {
                "TR_m1_M_MS_DaysLeft",
                "TR_m1_M_MS_K_Day",
                "TR_m1_M_MS_K_Month",
                "TR_m1_M_Rilki_DaysLeft",
                "TR_m1_M_Rilki_K_Day",
                "TR_m1_M_Rilki_K_Month",
                "TR_m2_Rent_TelGilan_Racer",
                "TR_m3_AT_Silsiplayer",
                "TR_m3_AT_TG_Q1_GoldCount",
                "TR_m3_EEC_GoldReward",
                "TR_m3_M_Dars_DaysLeft",
                "TR_m3_M_Dars_K_Day",
                "TR_m3_M_Dars_K_Month",
                "TR_m3_M_Eleri_DaysLeft",
                "TR_m3_M_Eleri_K_Day",
                "TR_m3_M_Eleri_K_Month",
                "TR_m3_M_Gartus_DaysLeft",
                "TR_m3_M_Gartus_K_Day",
                "TR_m3_M_Gartus_K_Month",
                "TR_m3_Nanaav_PCinVault",
                "TR_m3_Rent_Gorne",
                "TR_m3_Rent_INNSIDEOUT",
                "TR_m3_Rent_Marog_TTC",
                "TR_m3_Rent_Nanaav_PilgrimsHall",
                "TR_m3_Rent_Oth_Fern",
                "TR_m3_Rent_RD_Lodge",
                "TR_m3_Rent_Supai",
                "TR_m3_Rent_Varon_Hem",
                "TR_m4_AA_KuvatRent",
                "TR_m4_M_Llorala_DaysLeft",
                "TR_m4_M_Llorala_K_Day",
                "TR_m4_M_Llorala_K_Month",
                "TR_m4_Rent_Ando_Council_Club",
                "TR_m4_Rent_Bodrum",
                "TR_m4_Rent_Dancing_Cup",
                "TR_m4_Rent_Golden_Moons",
                "TR_m4_Rent_Grey_Lodge",
                "TR_m4_Rent_Guar_No_Name",
                "TR_m4_Rent_Lucky_Shalaasa",
                "TR_m4_Rent_Omaynis",
                "TR_m4_Rent_Teyn",
                "TR_m4_Rent_Uman",
                "TR_m4_TT_And_GoldBase",
                "TR_m4_TT_And_GoldCounter",
                "TR_m4_TT_And_GoldCounterB",
                "TR_m4_TT_And_ScripCounter",
                "TR_m7_HO_TT_01_Donations",
                "TR_m7_M_Amata_DaysLeft",
                "TR_m7_M_Amata_K_Day",
                "TR_m7_M_Amata_K_Month",
                "TR_m7_Ns_ArenaBetAmount",
                "TR_m7_Ns_ArenaCurrentDuel",
                "TR_m7_Ns_ScripGoldDiff",
                "TR_m7_Ns_ScripGoldSum",
                "TR_m7_Ns_TG_7_GoldReward",
                "TR_m7_Rent_AldMarak",
                "TR_m7_Rent_FightingChance",
                "TR_m7_Rent_HK_Hound",
                "TR_m7_Rent_HK_Mudcrab",
                "TR_m7_Rent_MaarBani",
                "TR_m7_Rent_Mavandes",
                "TR_m7_Rent_Narsis_Canyon",
                "TR_m7_Rent_Narsis_Dragon",
                "TR_m7_Rent_Narsis_Greenshade",
                "TR_m7_Rent_Narsis_LastDrop",
                "TR_m7_Rent_OthmuraMuck",
                "TR_m7_Rent_SapphireSlouch",
                "TR_m7_Rent_SeptG_Niben",
                "TR_m7_Rent_StormGP_Saxhleel",
            },
            quest = {
                "TR_HH_indorilspiritkill_glb",
                "TR_Hla_ScripCount",
                "TR_m0_HH_NilenoLetter",
                "TR_m0_YakinBaelQuestHook",
                "TR_m1_FG_Mashugsaved",
                "TR_m1_FG_Salirazkill",
                "TR_m1_FG_StalkerTrappedDay",
                "TR_m1_FW_IC6_LoreCount",
                "TR_m1_FW_MG01_Perm",
                "TR_m1_FW_TG6_TimesCaught",
                "TR_m1_FW_TG_StoneWorn",
                "TR_m1_FWMG_TathoState",
                "TR_m1_HT_DralDead",
                "TR_m1_HT_EldaleDead",
                "TR_m1_HT_FakeLedgerPlanted",
                "TR_m1_HT_FarunaDead",
                "TR_m1_HT_Rathra_q6_MineDead",
                "TR_m1_HT_RathraDead",
                "TR_m1_IC6_Daisychainglobal",
                "TR_m1_IL_cultistcount",
                "TR_m1_IL_Darnellgoonce",
                "TR_m1_IL_Darnelltalked",
                "TR_m1_IL_Darnellwin",
                "TR_m1_IL_guarstate",
                "TR_m1_Niv_JustASip_IncarnOwner",
                "TR_m1_q71_timeout",
                "TR_m1_q_ArthalDead",
                "TR_m1_q_DilnarDead",
                "TR_m1_q_VeranaDead",
                "TR_m1_RunningTroubleRewardFW",
                "TR_m1_TT_2_PTCommonersReward",
                "TR_m1_TT_2_PTSlavesReward",
                "TR_m1_TT_3_LTCommonersReward",
                "TR_m1_TT_5_DeadCount",
                "TR_m1_TT_5_RRTotalDealtWith",
                "TR_m1_TT_5_TimerOver",
                "TR_m1_TT_7_VampDead",
                "TR_m2_Ak_HrothdorDead",
                "TR_m2_Ak_HrothdorMad",
                "TR_m2_CantKill_MT",
                "TR_m2_FG_cultist_counter",
                "TR_m2_He_MG_LamplitSearch",
                "TR_m2_He_TG_Done",
                "TR_m2_HT_Vaerin_DaedraDead",
                "TR_m2_HT_Vaerin_Q6Done",
                "TR_m2_HT_Vaerin_RitualDone",
                "TR_m2_HT_VaerinDead",
                "TR_m2_MG_Aka_drimsu",
                "TR_m2_q_TheRiftDral",
                "TR_m2_WindbreakerSmugglerDead",
                "TR_m3_AT_CC_Majordomo",
                "TR_m3_AT_Deed_SlaveFreed",
                "TR_m3_AT_Deed_SlaveKilled",
                "TR_m3_AT_Literacy_Balmora",
                "TR_m3_AT_Literacy_Mournhold",
                "TR_m3_AT_Literacy_Vivec",
                "TR_m3_AT_RatFriend_Prison",
                "TR_m3_AT_TG_Q1_AggroCounter",
                "TR_m3_AT_TG_Q1_OrdWarning",
                "TR_m3_AT_TG_Q6_ThugsDeadCount",
                "TR_m3_AT_Toldmerchant",
                "TR_m3_BrothChain_Glb",
                "TR_m3_Brother_HelmDropped",
                "TR_m3_Brother_HelmTaken",
                "TR_m3_Brother_TrialReset",
                "TR_m3_Dar_NiernisOffer",
                "TR_m3_EEC_Cano3_TalkingTo",
                "TR_m3_EEC_CounterfeitCount",
                "TR_m3_EEC_CounterfeitReward",
                "TR_m3_EEC_GoldDelays",
                "TR_m3_EEC_GoldJodald",
                "TR_m3_EEC_GoldWayrian",
                "TR_m3_EEC_GoldYaguz",
                "TR_m3_EEC_MarkiFound",
                "TR_m3_EEC_RansomRequested",
                "TR_m3_EEC_SilverspoonTalks",
                "TR_m3_EEC_VarusoFreed",
                "TR_m3_EEC_YakFreed",
                "TR_m3_EEC_YontusFreed",
                "TR_m3_FG_OE3_PunavitTrip",
                "TR_m3_FG_OE8_FightState",
                "TR_m3_Hal_IndorilReward",
                "TR_m3_HI_G4_TitheCounter",
                "TR_m3_HI_MQ1_1_glob",
                "TR_m3_HI_RoaDyr2_SlaveStatus",
                "TR_m3_HI_RoaDyr5_Cure",
                "TR_m3_HI_RoaDyr7_rip",
                "TR_m3_IC_MoraelynRobe_perm",
                "TR_m3_IrvasaSorvasInformed",
                "TR_m3_KadanuranDeadCount",
                "TR_m3_Literacy_Balmora",
                "TR_m3_MG_OE_ErnandreMGStatus",
                "TR_m3_MG_OE_Q9_BigDaedraDead",
                "TR_m3_MoveRathysMadalvel",
                "TR_m3_MT_DilsNoteFound",
                "tr_m3_nnv_glob_museumandas",
                "tr_m3_nnv_glob_museumantumbral",
                "TR_m3_Nnv_glob_museumBone",
                "tr_m3_nnv_glob_museummeris",
                "tr_m3_nnv_glob_museumolms",
                "tr_m3_nnv_glob_museumradack",
                "tr_m3_nnv_glob_museumvisitant",
                "tr_m3_nnv_glob_museumwind",
                "tr_m3_nnv_glob_museumwounds",
                "TR_m3_OE_CourtWizardState",
                "TR_m3_OE_DAdelay",
                "TR_m3_OE_Pack_Guar_glob",
                "TR_m3_Ord_Doc1_PickAE",
                "TR_m3_Ord_Doc1_PickCO",
                "TR_m3_Ord_Doc1_PickFH",
                "TR_m3_Ord_Doc1_PickGD",
                "TR_m3_Oth_SlavesAvailableArg",
                "TR_m3_Oth_SlavesAvailableKha",
                "TR_m3_Oth_SlavesAvailableOth",
                "TR_m3_Oth_TT_GuardDiseaseCount",
                "TR_m3_Pilgrim_Ring_PickedUp",
                "TR_m3_q_AccPayDuelActive",
                "TR_m3_q_AccPayDuelWon",
                "TR_m3_q_AccPayRelic",
                "TR_m3_q_AdosiBiranDiscount",
                "TR_m3_q_AT_Armiger_Bid",
                "TR_m3_q_Dar_ParaVnKill",
                "TR_m3_q_DedicationFound",
                "TR_m3_q_Kassad_QuestionedNumbe",
                "TR_m3_q_NunSheiFound",
                "TR_m3_q_Oth_Alvana",
                "TR_m3_q_PecuniaFound",
                "TR_m3_q_RD_HlesGangDead",
                "TR_m3_q_RubberSold",
                "TR_m3_q_SilmanorFound",
                "TR_m3_q_UlvoAvethynFound",
                "TR_m3_q_Vh_BootsPickup",
                "TR_m3_Rils_AndelFound",
                "TR_m3_Sa_IdrenieDead",
                "TR_m3_Sa_TreramAlert",
                "TR_m3_SilhaenilAttacked",
                "TR_m3_Speaker_Tyval_killed",
                "TR_m3_TT_Dar_3_attacked",
                "TR_m3_TT_Dar_5_Nearby",
                "TR_m3_Var_Justice_AllowGlass",
                "TR_m3_Vh_Drathas_glb",
                "TR_m3_Vhul_SlavesAvailable",
                "TR_m3_Wil_CleanSweep_hasKey",
                "TR_m3_Wil_CleanSweep_SA_Slave",
                "TR_m3_WLCR_FightDeath",
                "TR_m3_WLCR_KidnappersDead",
                "TR_m3_WLCR_KidnappersGreet",
                "TR_m4_AA_GuarCounter",
                "TR_m4_AA_GuarNassuran",
                "TR_m4_AA_GuarSeresa",
                "TR_m4_AA_IssarbaddonHostile",
                "TR_m4_AA_JSuhkurrStatus",
                "TR_m4_AA_KhesurraStatus",
                "TR_m4_AA_TharmadalionStatus",
                "TR_m4_AA_UrnuridunGuarState",
                "TR_m4_And_Bounty_Bone_Dead",
                "TR_m4_And_Bounty_Holst_Dead",
                "TR_m4_And_Bounty_ra_Dead",
                "TR_m4_And_Bounty_Runat_Dead",
                "TR_m4_And_Bounty_Vyper_Dead",
                "TR_m4_And_MaterialMatters_g",
                "TR_m4_Ando_AlchemistsTimeCtrl",
                "TR_m4_Ando_HemmetteDetected",
                "TR_m4_Ando_HemmetteRumor",
                "TR_m4_Ando_NevusaOffer",
                "TR_m4_AndoHH_EggOffice",
                "TR_m4_AndoHH_GreefDay",
                "TR_m4_AndoHH_GreefHour",
                "TR_m4_AndoHH_LetterID",
                "TR_m4_AndoHH_MelsPay",
                "TR_m4_AndoHH_UnsealLetter",
                "TR_m4_AndoHH_ZalanDeath",
                "TR_m4_ArvJewelReady",
                "TR_m4_Bal0_ArmasSpoken",
                "TR_m4_Bal_AmuletDayUsed",
                "TR_m4_Bal_DevourerDead",
                "TR_m4_Bal_DevourerSpawned",
                "TR_m4_Bal_MagicalLink",
                "TR_m4_DredaseDevani",
                "TR_m4_FG_AlitGlobal",
                "TR_m4_FG_JubalGlobal",
                "TR_m4_FG_LlaranStateB",
                "TR_m4_FG_LlaranStateC",
                "TR_m4_FG_OrblosGlobal",
                "TR_m4_GavrosCT",
                "TR_m4_GavrosSixthHouse",
                "TR_m4_HH_AnbarysGuardsLeft",
                "TR_m4_HH_AND_HearingGlob",
                "TR_m4_HH_AndasPoint",
                "TR_m4_HH_CaravanReleaseDay",
                "TR_m4_HH_NalvosAlvuru",
                "TR_m4_HH_NalvosDrink",
                "TR_m4_HH_NalvosRedoran",
                "TR_m4_HH_OlvysAccept",
                "TR_m4_HH_TeraniRescued",
                "TR_m4_HH_Ulvo1_LedgerRead",
                "TR_m4_HH_WorkerDeathCounter",
                "TR_m4_IL_arveladvice",
                "TR_m4_IL_dalsadvice",
                "TR_m4_IL_darraadvice",
                "TR_m4_IL_grudgeplan",
                "TR_m4_IL_JizirrFreed",
                "TR_m4_LostTransit_AshDead_glb",
                "TR_m4_LostTransit_Control_glb",
                "TR_m4_LostTransit_Days_glb",
                "TR_m4_namirachest_glb",
                "TR_m4_Om_ReRe_ConfCnt",
                "TR_m4_Om_ReRe_FacReq",
                "TR_m4_Om_ReRe_ForemanConf",
                "TR_m4_Om_ReRe_KillCount",
                "TR_m4_Om_ReRe_Ordinators",
                "TR_m4_Ord_Rite_deadcount",
                "TR_m4_OssurClannfearGlobal",
                "TR_m4_q_AG_ancestorFound",
                "TR_m4_q_AG_candidate",
                "TR_m4_q_AG_Votes",
                "TR_m4_q_Credkill",
                "TR_m4_q_drowned_infoalch",
                "TR_m4_q_drowned_infoissmi",
                "TR_m4_q_drowned_infomages",
                "TR_m4_q_drowned_infopriest",
                "TR_m4_q_drowned_timepassed",
                "TR_m4_q_Euphoria_RingUsed",
                "TR_m4_q_invadersdead_glb",
                "TR_m4_q_mm_Ashvudead",
                "TR_m4_q_PickledFishStolen",
                "TR_m4_q_PW_HibdunDrank",
                "TR_m4_q_PW_ToldMusa",
                "TR_m4_q_TTSCoraneDead",
                "TR_m4_q_TTSFranDead",
                "TR_m4_q_TTSGhostDead",
                "TR_m4_q_vamplure_glb",
                "TR_m4_q_WWWtalked",
                "TR_m4_RR_ArgoBanditGlob",
                "TR_m4_SpokeToDarane",
                "TR_m4_T_Nuccius_Alomon_Status",
                "TR_m4_T_Nuccius_Sujamma1",
                "TR_m4_T_Nuccius_Sujamma2",
                "TR_m4_T_Nuccius_Sujamma3",
                "TR_m4_TG_Ando_JoKaarFree",
                "TR_m4_TG_AndoAttack",
                "TR_m4_TG_AndoBaseTraps",
                "TR_m4_TG_AndoSquatterSkooma",
                "TR_m4_TG_ArantamoDispChange",
                "TR_m4_TG_SheiAdvGlb",
                "TR_m4_TG_SquatterDead",
                "TR_m4_TG_ThoriclesCheck",
                "TR_m4_TG_VilungilLedger_Read",
                "TR_m4_TT_DepartDay",
                "TR_m4_TT_TakeItems",
                "TR_m4_Uman_B2_DiraBrought",
                "TR_m4_VA_DeadAndas",
                "TR_m4_VA_deadhlaalu_glb",
                "TR_m4_VA_Ibinai_Truth",
                "TR_m4_VA_resetscout",
                "TR_m4_VA_spellsword_glb",
                "TR_m4_VethaLlathsa_sum_glb",
                "TR_m4_Vf_Mabrigash_Count",
                "TR_m4_Vf_SenipalFreed",
                "TR_m4_Vf_TimerGlb",
                "TR_m4_VysAssaPurged",
                "TR_m4_WillToGoOn_Dead",
                "TR_m4_WLCR_GuardsWarn",
                "TR_m7_AI_JNS_3_BaryonGlob",
                "TR_m7_AI_JNS_4_Glob",
                "TR_m7_AI_JNS_6_Glob",
                "TR_m7_AI_JNS_6_MarisFollow",
                "TR_m7_AI_JNS_7_Alert",
                "TR_m7_AI_TT_HlerasConfrBefore",
                "TR_m7_AI_TT_MuransFrustration",
                "TR_m7_AT_TG_Q6_Council_Booze",
                "TR_m7_EEC_Caravandelay",
                "TR_m7_EEC_farmerdeadcount",
                "TR_m7_EEC_Feast",
                "TR_m7_EEC_killfarmers_glob",
                "TR_m7_EEC_Narsis2_itemcount",
                "TR_m7_EEC_Narsis3_weightsplace",
                "TR_m7_EEC_Narsis_Book",
                "TR_m7_EEC_Narsis_Helm",
                "TR_m7_EEC_Narsis_Hide",
                "TR_m7_EEC_Narsis_Pot",
                "TR_m7_EEC_Narsis_Saltrice",
                "TR_m7_EEC_Narsis_Shield",
                "TR_m7_EEC_Narsis_Skirt",
                "TR_m7_EEC_Shinathidead",
                "TR_m7_EEC_SickGuar",
                "TR_m7_EEC_Tongdead",
                "TR_m7_EssurnashpiDeadCount",
                "TR_m7_FamilyAffair_RavosReturn",
                "TR_m7_FamilyAffair_TalkUlyne",
                "TR_m7_HH_Alvynu2_Murder",
                "TR_m7_HH_Alvynu_4_SpeakerGhost",
                "TR_m7_HH_Alvynu_5_AssState",
                "TR_m7_HH_Alvynu_6_SpiritsDealt",
                "TR_m7_HH_Alvynu_7_CCKilled",
                "TR_m7_HH_AxeReturn",
                "TR_m7_HH_Dren_q_Orvas1_Seal",
                "TR_m7_HH_Dren_q_Vedam1_Seal",
                "TR_m7_HH_ElarFramed",
                "TR_m7_HH_FirstFamily_smug",
                "TR_m7_HH_fixbreach",
                "TR_m7_HH_grandmasterask",
                "TR_m7_HH_HasGuarLiver",
                "TR_m7_HH_invest_bank",
                "TR_m7_HH_invest_dravos",
                "TR_m7_HH_invest_nord",
                "TR_m7_HH_lefttocollect",
                "TR_m7_HH_SvoshGlobal",
                "TR_m7_HH_UnsealedLlananu",
                "TR_m7_HindsightPlanted",
                "TR_m7_HL_FG_Q2_DeadCount",
                "TR_m7_HL_FG_Q4_FightStarted",
                "TR_m7_HL_FG_Q5_DeadCount",
                "TR_m7_HL_FG_Q5_SlavesFreed",
                "TR_m7_HL_FG_Q6_DeadCount",
                "TR_m7_HL_FG_Q6_FightStarted",
                "TR_m7_HO_HFS_HouseOffer",
                "TR_m7_HO_HighAndDry_CrewKilled",
                "TR_m7_HO_TT_05_askedIda",
                "TR_m7_HO_TT_05_askedMG",
                "TR_m7_HO_TT_05_crooknotefound",
                "TR_m7_HO_TT_07_DadrysAngry",
                "TR_m7_HOGuideEnt",
                "TR_m7_HOGuideInf",
                "TR_m7_HR_AldIuval_q1_ShojaFree",
                "TR_m7_HR_AM_Q1_AtemuNixDeath",
                "TR_m7_HR_AM_Q2_Shellcountdown",
                "TR_M7_HR_AM_Q2_ShellDone",
                "TR_m7_HR_AM_Q4_BanditDead",
                "TR_m7_HR_WM_NilusDead",
                "TR_m7_IC_FeedPoorCountAI",
                "TR_m7_IC_HL_AlmsAnnalina",
                "TR_m7_IC_HL_AlmsBolnor",
                "TR_m7_IC_HL_AlmsCorter",
                "TR_m7_IC_HL_AlmsDithisi",
                "TR_m7_IC_HL_AlmsDoZhisi",
                "TR_m7_IC_HL_AlmsHvithar",
                "TR_m7_IC_HL_AlmsKhazor",
                "TR_m7_IC_HL_AlmsLinriah",
                "TR_m7_IC_HL_AlmsPeleri",
                "TR_m7_IC_HL_AlmsRuvene",
                "TR_m7_IC_HL_AlmsTaderi",
                "tr_m7_Jhasa-DarFreed",
                "TR_m7_JNS_TheBoss_HammerReward",
                "TR_m7_JNS_TheBossAttacked",
                "TR_m7_JNS_UddanuDaeCrossbowGlb",
                "TR_m7_Kalk_Fight_GoblinDead",
                "TR_m7_Kalk_Fight_OrcDead",
                "TR_m7_KurikiDeadCount",
                "TR_m7_MakingPeace_Return",
                "TR_m7_Narsis_SM_killed",
                "TR_m7_nightmotherattack",
                "TR_m7_Ns_ArenaBeast",
                "TR_m7_Ns_ArenaFightChoice",
                "TR_m7_Ns_ArenaGenericFight",
                "TR_m7_Ns_BilchabulBetray",
                "TR_m7_Ns_BilchabulSuryvnAngry",
                "TR_m7_Ns_Caravan_Casino",
                "TR_m7_Ns_Caravan_Key",
                "TR_m7_Ns_FG_2_HasSilver",
                "TR_m7_Ns_FG_6_MinerState",
                "TR_m7_Ns_FortunaQuestActive",
                "TR_m7_Ns_HH_SarithaSeal",
                "TR_m7_Ns_IC6_SavePriest",
                "TR_m7_Ns_IL_3_Culprit",
                "TR_m7_Ns_IL_3_Glob",
                "TR_m7_Ns_IL_6_VanikenDeadGlb",
                "TR_m7_Ns_IL_7_Raid",
                "TR_m7_Ns_JNS_2_SoldGreydust",
                "TR_m7_Ns_JNS_3_GuardiansKilled",
                "TR_m7_Ns_JNS_4_AnderaFollow",
                "TR_m7_Ns_JNS_4_Caught",
                "TR_m7_Ns_JNS_4_FollowState",
                "TR_m7_Ns_JNS_4_JandieGangKille",
                "TR_m7_Ns_JNS_4_ShouldFollow",
                "TR_m7_Ns_JNS_7_AttackTong",
                "TR_m7_Ns_JNS_7_NegotiatorsDead",
                "TR_m7_Ns_MG_QuestsDone_glb",
                "TR_m7_Ns_MoonshinerEnds",
                "TR_m7_Ns_NucciusRumor",
                "TR_m7_Ns_SlavesAvailableArg",
                "TR_m7_Ns_SlavesAvailableKha",
                "TR_m7_Ns_SlavesAvailableOth",
                "TR_m7_Ns_StageFright_ActorKill",
                "TR_m7_Ns_StageFright_Mistakes",
                "TR_m7_Ns_TG_4_Greydust",
                "TR_m7_Ns_TG_7_GoldIngots",
                "TR_m7_Ns_TG_8_HeistComplete",
                "TR_m7_Ns_ToTheLastDropFound",
                "TR_m7_NsTT_ShinApostKillCount",
                "TR_m7_NymphAttack",
                "TR_m7_Oth_BrothersLove_Glob",
                "TR_m7_Oth_MG_1_SquatterDead",
                "TR_m7_Oth_MG_1_SquatterState",
                "TR_m7_Oth_MG_ApproachEndRoom",
                "TR_m7_Oth_MG_ApproachSigilBarr",
                "TR_m7_Oth_MG_ApproachTeleKey",
                "tr_m7_oth_q_eldavelletter_seal",
                "TR_m7_q_AI_OrnadaCured",
                "TR_m7_q_CE_DetectedByWorker",
                "TR_m7_q_CE_Dividends",
                "TR_m7_q_CE_WorkersBought",
                "TR_m7_q_CE_WorkersCollected",
                "TR_m7_q_LetterToSodreru_seal",
                "TR_m7_q_rarethil_intimidated",
                "TR_m7_q_Zarathil_Smugglers",
                "TR_m7_Sdr_Defiling_MuranAsked",
                "TR_m7_Sharai_WinchesterStolen",
                "TR_m7_ShiSha_HH_Q4Flin",
                "TR_m7_SM_AlembicFound",
                "TR_m7_SM_CalcinatorFound",
                "TR_m7_SM_MortarFound",
                "TR_m7_SM_QuestStarted",
                "TR_m7_SM_RetortFound",
                "TR_m7_SuppliesSDPiratesGlobal",
                "TR_m7_TejNaFound",
                "TR_m7_VA_AniVamp_InfectCount",
                "TR_m7_VurvynOthrenDead",
                "TR_NecMQ_BoethiahRep",
                "TR_NecMQ_CalmOrd",
                "TR_NecMQ_ICDestroyerRitual",
                "TR_NecMQ_ICMadmanRitual",
                "TR_NecMQ_MephalaRep",
                "TR_NecMQ_SchemerState",
                "TR_orlukhkillcount",
            },
            kills = {
            },
            factionRanks = {
                "TR_Fac_NerevarineCount",
                "TR_m4_AndoHH_CTRating1",
                "TR_m4_AndoHH_CTRating2",
            },
            factionExpulsion = {
                "TR_m7_HO_TT_04_MegaExpelled",
            },
            worldwide = {
                "TR_FM_Glob_State",
                "TR_FM_Glob_Weather",
                "TR_HH_GM_enable_trainers",
                "TR_m1_FG_StalkerTrapped",
                "TR_m1_FW_BrazenCrateSeen",
                "TR_m1_FW_TG6_Attack",
                "TR_m1_FW_TG6_BoneBroken",
                "TR_m1_IL_Arloteleported",
                "TR_m1_q_Bthal_CrystalTarget",
                "TR_m1_q_Bthal_CrystalTracker",
                "TR_m1_q_Bthalagstate",
                "TR_m2_MzankhDoorState",
                "TR_m2_q_Nm_Wake_done",
                "TR_m3_AT_TG_Q6_ThugsDisable",
                "TR_m3_BtharchPower",
                "TR_m3_DeadintheWater_Start",
                "TR_m3_fiendrandomizer_glb",
                "TR_m3_Hal_TowerState",
                "TR_m3_IL_Judge_OlresLeave",
                "TR_m3_KatariahMaskPickedUp",
                "TR_m3_KZ_Power",
                "TR_m3_MG_OE_Q7_CeilingOpen",
                "TR_m3_MG_OE_Q9_PortalOpen",
                "TR_m3_MoraTombAshes",
                "TR_m3_Nanaav_VaultGuards",
                "tr_m3_nnv_glob_museumwaters",
                "TR_m3_OE_GB_coffinopen",
                "TR_m3_OE_RumaDead",
                "TR_m3_OE_TowerGhostTimer",
                "TR_m3_Oth_TT_Q4_Score",
                "TR_m3_q_cane_activated",
                "TR_m3_speakercandle_choice",
                "TR_m3_TT_Dar_2_Open",
                "TR_m3_TT_SpeakerState",
                "TR_m3_TT_StSeryn_2_ChaseGlobal",
                "TR_m3_veloth_shrine_spawncount",
                "TR_m4_AA_BuriedSilver",
                "TR_m4_AA_DamiloBodyReveal",
                "TR_m4_AA_SehutuAggro",
                "TR_m4_AA_SehutuDead",
                "TR_m4_AA_SehutuFriend",
                "TR_m4_And_ArmisticeTyrusDetect",
                "TR_m4_And_IncThreat_SkullDrawer",
                "TR_m4_And_SheKindlySpoke_Stash",
                "TR_m4_Andas_VaultBreakInGlobal",
                "TR_m4_AndasLiftGlobal1",
                "TR_m4_AndasLiftGlobal2",
                "TR_m4_AndasSewerAccess",
                "TR_m4_AndasSewerGateState",
                "TR_m4_AndasTombFlame",
                "TR_m4_AndasTombState",
                "TR_m4_Ando_HemmetteArrestDay",
                "TR_m4_Ando_HemmetteDead",
                "TR_m4_Ando_HemmettePrison",
                "TR_m4_Ando_NevusaDead",
                "TR_m4_Ando_NevusaDeskState",
                "TR_m4_Ando_NevusaPotionPlaced",
                "TR_m4_AndoHH_CrateRemove",
                "TR_m4_AndoHH_EggStorage",
                "TR_m4_AndoHH_NalvynaReward",
                "TR_m4_AndoHH_ShipDisable",
                "TR_m4_AndoHH_ShipReleased",
                "TR_m4_AndoHH_ShipTrespass",
                "TR_m4_AndoHH_SujammaCheck",
                "TR_m4_Andoth_AndasVaultGlobal",
                "TR_m4_AndoWharfHideoutGlobal",
                "TR_m4_BahrundGlobal",
                "TR_m4_Bal_PowerChosen",
                "TR_m4_BthungthuvDoorGlobal",
                "TR_m4_FelmsLiftGlobal1",
                "TR_m4_FelmsLiftGlobal2",
                "TR_m4_FG_AndasInVault",
                "TR_m4_FG_UshuFree",
                "TR_m4_HH_SaboteurAction",
                "TR_m4_HH_SavrethiAlive",
                "TR_m4_HH_WorkerDisable",
                "TR_m4_MenaanTowerDoorGlobal",
                "TR_m4_NirnBoundGlobal1",
                "TR_m4_Oma_InnStage",
                "TR_m4_Ord_Rite_state",
                "TR_m4_orlukhgate02_glb",
                "TR_m4_q_AAB_moneystolen",
                "TR_m4_q_TMM_Bolsleave",
                "TR_m4_ShalmuratGateAccess",
                "TR_m4_Shenjirra_Gate",
                "TR_m4_SkelWiz_HlaaluEnabled",
                "TR_m4_T_Nuccius_Delay",
                "TR_m4_TG_AncylisDoorGlobal",
                "TR_m4_TG_AndoBaseBanners",
                "TR_m4_TG_AndoBaseBar",
                "TR_m4_TG_AndoBaseBeds",
                "TR_m4_TG_AndoBaseCleanUp",
                "TR_m4_TG_AndoBaseLiftGlobal",
                "TR_m4_TG_AndoBaseLiftGlobal2",
                "TR_m4_TG_AndoBasePlants",
                "TR_m4_TG_AndoBaseRugs",
                "TR_m4_TG_AndoBaseTraining",
                "TR_m4_TG_AndoShei6Global",
                "TR_m4_TG_AndoSideQuest",
                "TR_m4_TG_AndoSkoomaCat",
                "TR_m4_TG_UrnCounter",
                "TR_m4_TJ_IndSpiritKill",
                "TR_m4_TT_AndothrenFinale",
                "TR_m4_TT_OrcHostile",
                "TR_m4_UshuKurLiftGlobal",
                "TR_m4_UshuKurLiftGlobal2",
                "TR_m4_UshuKurLiftGlobal3",
                "TR_m4_UshuKurLiftGlobal4",
                "TR_m4_VA_AndasGreetOnce",
                "TR_m4_VA_budaktrigger",
                "TR_m4_vampvictim_incell",
                "TR_m4_WillToGoOn_Willpower",
                "TR_m7_AI_TT_HouseSupport",
                "TR_m7_FelvynGangEnable",
                "TR_m7_HH_Alvynu2_SlipInDesk",
                "TR_m7_HH_Alvynu_4_IlvrinActive",
                "TR_m7_HH_Alvynu_6_TombState",
                "TR_m7_HH_Alvynu_7_FlsgrtActive",
                "TR_m7_HH_Alvynu_7_ShipTimer",
                "TR_m7_HOGuideRuin",
                "TR_m7_IbbiSuen_MineState",
                "TR_m7_LA4_EggMineStage",
                "TR_m7_Narsis_2ndVaultPerm",
                "TR_m7_Ns_ArenaCorrectDoor",
                "TR_m7_Ns_ArenaDuelActive",
                "TR_m7_Ns_ArenaDuelBegun",
                "TR_m7_Ns_FortunaAllowAccess",
                "TR_m7_Ns_FortunaTongDisable",
                "TR_m7_Ns_MG_Cont_glb",
                "TR_m7_Ns_RescuePotLady_Moved",
                "TR_m7_Ns_StageFright_ActState",
                "TR_m7_Oth_MG_ApproachWaterRoom",
                "TR_m7_Oth_MG_MainDoorUnlocked",
                "TR_m7_Oth_MG_RubbleCleared",
                "TR_m7_Oth_MG_TeleDoorUnlocked",
                "TR_m7_Proconsul_dead_glb",
                "TR_m7_Sharai_TL_Access",
                "TR_m7_ShiSha_Haunt_CleanCount",
                "TR_m7_VA_AniVamp_TestState",
                "TR_Necrom_FanePortL",
                "TR_Necrom_FanePortR",
            }
        }
    }
    tableHelper.merge(clientVariableScopes, c21VariableScopes, true)
end



-- ArenaMP Y043: MFR.esm variable scopes.
-- Only persistent player/quest/world state is synchronized. Volatile MFR timers
-- stay unsynchronized to avoid the packet spam that a blanket 303-GLOB import
-- would create.
if tableHelper.containsCaseInsensitiveString(clientDataFiles, "MFR.esm") then
    local mfrVariableScopes = {
        globals = {
            ignored = {
                "al_target_active", "trav_viv_out", "al_CraftTemp", "al_CraftUnluck",
                "al_OptionStep", "al_CustomStart", "ab01debug", "CG_Temp",
                "CG_TempQ", "al_BoneGameTemp", "al_SorterGlobal", "al_MFRGlobal",
                "al_mpregen_check", "al_DifficultyMod", "al_travGlobal", "aL_Retroactive",
                "al_CraftGlobal", "al_QuestFalmer", "al_QuestGMCald", "al_QuestPelagiad",
                "al_QuestCent", "al_QuestClagius", "al_QuestNirnroot", "al_QuestHeladren",
                "al_QuestCalia1", "al_QuestCalia2", "al_QuestEndyne", "al_QuestFireAnud",
                "al_QuestArena", "al_QuestSkyrim", "al_QuestTelMora", "al_LocGDR",
                "al_LowerLevels",
            },
            personal = {
                "CREL_VanillaStart", "CREL_isRandom", "CREL_overallState", "CREL_uniqueText",
                "CREL_setVvaScen", "CREL_setSolScen", "CREL_setMorScen", "CREL_uTextMor",
                "CREL_uTextSol", "CREL_uTextVva", "CREL_setCyrScen", "CREL_setSkyScen",
                "CREL_itemEdRand", "CREL_spellEdRand", "CREL_randStart", "CREL_quickChoice",
                "CREL_compEdRand", "CREL_compWeapon", "CREL_compArmor", "CREL_compMagic",
                "CREL_setCompanion", "CREL_compMagicB", "CREL_compLongbl", "CREL_compShortb",
                "CREL_compBluntw", "CREL_compAxe", "CREL_compSpear", "CREL_compMarks",
                "CREL_compHandto", "CREL_compHeavya", "CREL_compMedium", "CREL_compLighta",
                "CREL_compIllus", "CREL_compAlter", "CREL_compConju", "CREL_compDestr",
                "CREL_compMysti", "CREL_compResto", "CREL_compStren", "CREL_compSpeed",
                "CREL_compIntel", "CREL_compEndur", "CREL_compWillP", "CREL_compPerso",
                "CREL_compAgili", "CREL_compLuck", "CREL_compHealth", "CREL_compMagicka",
                "CREL_compFatigue", "CREL_compStatus", "CREL_compStart", "CREL_compLevel",
                "CREL_compPet", "CREL_noShirt", "CREL_noPants", "CREL_petStren",
                "CREL_petSpeed", "CREL_petIntel", "CREL_petEndur", "CREL_petWillP",
                "CREL_petPerso", "CREL_petAgili", "CREL_petLuck", "CREL_petHealth",
                "CREL_petMagicka", "CREL_petFatigue", "CREL_itemEdWeapon", "CREL_itemEdArmor",
                "CREL_itemEdClothes", "CREL_itemEdClothesB", "CREL_factionEdRand", "CREL_factionChosen",
                "CREL_compHealer", "CREL_setVampWere", "CREL_hostileScen", "CREL_compUnarm",
                "KPCMagickaMultiplier", "KPCMagickaReturnBase", "KPCMagickaReturnMult", "KPCMagickaRegenEnabled",
                "al_craft_armorer", "al_craft_jewerly", "al_craft_clothier", "al_craft_weapon",
                "al_Clothier", "al_Jewerly", "al_WeaponCraft", "al_ArmorCraft",
                "SW_PCRep", "CompCaliaLost", "GuarOwner", "GuarKMove",
                "GuarKtempHealth", "GuarKtempHealthRatio", "GuarNotWarping", "Rent_Seyda_Neen_Arille",
                "Rent_Khuul_Thongar", "Rent_Suran_Desele", "Rent_Dagonfel_End", "Rent_Gnisis_Madach",
                "Rent_Suran_Trade", "Rent_Molagmar_Hostel", "Rent_Molagmar_Pilgrim", "Rent_Druegh-jiggers_Rest",
                "Rent_Ald_Velothi",
            },
            quest = {
                "al_mg_soul", "al_mg_thing", "al_mg_armor", "al_mg_error",
                "al_mg_stage", "al_pir_gl", "al_ClanPotSteal", "al_bk_ajira1",
                "al_bk_ajira2", "al_alchemy_BLM_count", "al_alchemy_BLM_npc", "al_alchemy_BLM_item",
                "al_alchemy_BLM", "CraftQuestNpc", "CraftQuestIron", "CraftIronItem",
                "CraftIronCount", "CraftIronMake", "CraftIronMakeCount", "CraftResourse",
                "Room_fg_balm", "Room_hlaalu_balm", "Room_redoran_ald", "Room_telv_sadr",
                "Room_ic_ebon", "Room_il_gnis", "Room_gm_cald", "Room_gm_balm",
                "Room_mt_vivec", "Room_gt_balm", "Room_temple_ald", "al_Nartise",
                "al_TribTransport", "mbgCalDisp", "mbgAcq", "mbgCaliaGreet",
                "mbgCaliaFull", "OAAB_global_TMora_grown", "OAAB_global_TMora_stalkersdead", "OAAB_global_TMora_memstones",
                "clangerGlobal", "ArenaGrandChampion", "ArenaWeek", "ArenaChampRandom",
                "ArenaChampDead", "Arena_BattleNumber", "Arena_Bet_Disable", "Arena_Partner_Global",
                "Arena_werewolf", "ArenaCompanionFollowing", "Arena_GrandDead", "Arena_SpectatorDisable",
                "Arena_CuirassChange", "Arena_MonsterNumber", "Arena_MonsterCount", "Arena_MonsterDead",
                "Arena_MonsterKills", "ArenaMWeek", "Arena_StorePrepare", "Arena_TM_R",
                "Arena_TM_B", "Arena_TM_F", "Arena_TM_V", "Arena_TM_D",
                "Arena_TM_H", "Arena_TM_A", "Arena_challenged_once", "Arena_Team_Partner",
                "Arena_Team_Champ", "Arena_HardMode", "Arena_FinalMusic", "Arena_NoCuirass",
                "Arena_Gore",
            },
            worldwide = {
                "AB_LevitationDisabled", "AB_TeleportDisabled", "al_mzuleft_01_wall", "al_mzuleft_02_gateup",
                "al_mzuleft_broke_valve", "al_rockwall_state", "al_dno_lift_state", "al_dnv_isdown",
                "al_dnv_aredown", "al_dnv_rolled", "al_dnv_ifdown", "al_dn_creature_on",
                "al_dn_creature_ison", "al_force_cage_on", "al_dn_dagoth_wake", "al_akul_lava_rise",
                "al_dno_lava_down", "al_dn_dagoth_rest", "al_portal_active", "al_dnv_cagedown",
                "al_dnv_forceslide", "al_dno_pitdoor",
            }
        }
    }
    tableHelper.merge(clientVariableScopes, mfrVariableScopes, true)
end

return clientVariableScopes
