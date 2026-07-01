-- =========================================================================
-- 0) VÉRIFICATION PRÉALABLE — à exécuter d'abord.
--    Si une des requêtes ci-dessous retourne une ligne, l'ID 900000 est
--    déjà pris dans cette table : choisis un autre ID (ex: 900001) et
--    remplace-le partout dans ce fichier ET dans VOTE_GOSSIP_TEXTID
--    (npc_vote_reward.cpp) le cas échéant.
-- =========================================================================
-- SELECT * FROM `R1_Hotfixe`.`broadcast_text` WHERE `ID` = 900000;
-- SELECT * FROM `R1_World`.`npc_text` WHERE `ID` = 900000;
-- SELECT * FROM `R1_World`.`creature_template` WHERE `entry` = 900000;

-- =========================================================================
-- 1) BASE "characters" (R1_Chars) — progression quotidienne par compte
-- =========================================================================
CREATE TABLE IF NOT EXISTS `account_vote_reward` (
  `account_id`     INT UNSIGNED NOT NULL,
  `vote_day`       DATE NOT NULL,
  `dungeons_done`  TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `raids_done`     TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `claimed`        TINYINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`account_id`, `vote_day`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- =========================================================================
-- 2) BASE "world" — liste des boss finaux qui valident un donjon/raid
--    Remplis cette table avec l'entry du DERNIER boss de chaque instance
--    que tu veux éligible aux points de vote.
-- =========================================================================
CREATE TABLE IF NOT EXISTS `vote_reward_final_boss` (
  `creature_entry` INT UNSIGNED NOT NULL,
  `instance_type`  ENUM('dungeon','raid') NOT NULL,
  PRIMARY KEY (`creature_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Exemples à adapter avec les vrais entry de tes boss (creature_template.entry) :
-- INSERT INTO `vote_reward_final_boss` (`creature_entry`, `instance_type`) VALUES
-- (12345, 'dungeon'),  -- ex: dernier boss d'un donjon
-- (23456, 'raid');     -- ex: dernier boss d'un raid

-- =========================================================================
-- 3) BASE "world" — texte de gossip du PNJ
--    Dans ta version, npc_text ne stocke pas le texte directement : il
--    référence une entrée broadcast_text via BroadcastTextID0.
--    ÉTAPE A : créer l'entrée broadcast_text (voir plus bas, à ajuster
--    selon la structure exacte de ta table broadcast_text).
--    ÉTAPE B : faire pointer npc_text dessus.
-- =========================================================================

-- ÉTAPE A : entrée broadcast_text (base R1_Hotfixe, PAS R1_World)
DELETE FROM `R1_Hotfixe`.`broadcast_text` WHERE `ID` = 900000;
INSERT INTO `R1_Hotfixe`.`broadcast_text`
  (`ID`, `Text`, `Text1`, `LanguageID`, `ConditionID`, `Flags`, `VerifiedBuild`)
VALUES
  (900000, 'Vous avez voté pour Légion Éternelle ? Venez récupérer vos points ici !', '', 0, 0, 0, 0);

-- ÉTAPE B :
DELETE FROM `npc_text` WHERE `ID` = 900000;
INSERT INTO `npc_text` (`ID`, `Probability0`, `BroadcastTextID0`)
VALUES (900000, 1, 900000);

-- =========================================================================
-- 4) BASE "world" — creature_template du PNJ
--    Adapte l'entry (900000), le nom, le modèle (`Modelid1`), etc.
-- =========================================================================
DELETE FROM `creature_template` WHERE `entry` = 900000;
INSERT INTO `creature_template`
  (`entry`, `name`, `subname`, `IconName`, `minlevel`, `maxlevel`, `faction`,
   `npcflag`, `speed_walk`, `speed_run`, `scale`, `rank`, `dmgschool`,
   `BaseAttackTime`, `RangeAttackTime`, `unit_class`, `unit_flags`,
   `type`, `type_flags`, `lootid`, `movementType`, `HoverHeight`, `AIName`, `ScriptName`)
VALUES
  (900000, 'PNJ de Vote', 'Points quotidiens', '', 1, 1, 35,
   1, 1, 1.14286, 1, 0, 0,
   2000, 2000, 1, 0,
   7, 0, 0, 0, 1, '', 'npc_vote_reward');
-- npcflag = 1 -> NPCFLAG_GOSSIP
-- type = 7    -> CREATURE_TYPE_NON_COMBAT_PET n'est pas ça : mets 7 = Humanoid en général,
--                adapte selon les valeurs déjà utilisées dans ta base si besoin.

-- =========================================================================
-- 5) BASE "world" — spawns dans les capitales
--    Coordonnées à adapter précisément à l'endroit choisi. Voici des
--    points de départ génériques (à ajuster en jeu avec .npc add ou
--    en te téléportant et récupérant tes coordonnées avec .gps).
-- =========================================================================
-- Orgrimmar (Horde)
INSERT INTO `creature` (`id1`, `map`, `zoneId`, `areaId`, `spawnMask`, `phaseMask`,
                         `position_x`, `position_y`, `position_z`, `orientation`,
                         `spawntimesecs`, `wander_distance`, `currentwaypoint`,
                         `curhealth`, `curmana`, `MovementType`)
VALUES
  (900000, 1, 0, 0, 1, 1,
   1569.0, -4397.0, 16.0, 0,
   300, 0, 0, 1, 1, 0);

-- Hurlevent / Stormwind (Alliance)
INSERT INTO `creature` (`id1`, `map`, `zoneId`, `areaId`, `spawnMask`, `phaseMask`,
                         `position_x`, `position_y`, `position_z`, `orientation`,
                         `spawntimesecs`, `wander_distance`, `currentwaypoint`,
                         `curhealth`, `curmana`, `MovementType`)
VALUES
  (900000, 0, 0, 0, 1, 1,
   -8833.0, 628.0, 94.0, 0,
   300, 0, 0, 1, 1, 0);

-- NOTE : la structure exacte de la table `creature` (colonnes) peut varier
-- légèrement selon la version de LegionCore. Si l'INSERT échoue, envoie-moi
-- l'erreur ou la structure de ta table `creature` (DESCRIBE creature;) et
-- j'ajuste les colonnes.
