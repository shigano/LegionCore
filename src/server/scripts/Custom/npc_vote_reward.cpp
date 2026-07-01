/*
 * npc_vote_reward.cpp
 * -----------------------------------------------------------------------
 * PNJ de récupération des points de vote quotidiens (Légion Éternelle).
 *
 * Fonctionnement :
 *  - Le "jour de vote" démarre à 6h00 (heure du serveur) et se termine
 *    à 6h00 le lendemain. Un joueur ne peut réclamer qu'une fois par
 *    jour de vote.
 *  - Points de base : 3
 *  - Bonus +2 points si le compte a terminé au moins 2 donjons dans le
 *    jour de vote en cours.
 *  - Bonus +5 points si le compte a terminé au moins 1 raid dans le
 *    jour de vote en cours.
 *  - Les donjons/raids sont détectés via la mort du "boss final",
 *    référencé dans la table `vote_reward_final_boss` (base world).
 *  - Les points sont crédités sur `battlenet_accounts.battlePayCredits`
 *    (base auth / LoginDatabase), pour rester cohérent avec dashboard.php.
 *
 * Progression suivie par COMPTE (account_id), pas par personnage, afin
 * que les donjons/raids faits sur différents personnages du même compte
 * comptent pour le même objectif quotidien.
 *
 * Dépendances SQL : voir vote_reward_install.sql
 * -----------------------------------------------------------------------
 */

#include "ScriptMgr.h"
#include "Player.h"
#include "Creature.h"
#include "Map.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "WorldSession.h"
#include "ScriptedGossip.h"
#include <ctime>
#include <string>

// ------------------------------------------------------------------
// CONFIGURATION
// ------------------------------------------------------------------
enum VoteRewardConfig
{
    VOTE_BASE_POINTS   = 3,
    VOTE_DUNGEON_GOAL  = 2,
    VOTE_DUNGEON_BONUS = 2,
    VOTE_RAID_GOAL     = 1,
    VOTE_RAID_BONUS    = 5,

    // ID de npc_text (table world) utilisé pour le texte d'accueil du PNJ.
    // Voir vote_reward_install.sql pour créer cette entrée.
    VOTE_GOSSIP_TEXTID = 900000,

    VOTE_ACTION_CLAIM  = 1,
    VOTE_ACTION_CLOSE  = 2
};

// ------------------------------------------------------------------
// Calcule le "jour de vote" courant (format YYYY-MM-DD), avec un
// changement de jour à 6h00 du matin (heure locale du serveur) au lieu
// de minuit.
// ------------------------------------------------------------------
static std::string GetCurrentVoteDay()
{
    time_t now = time(nullptr);
    tm localTm{};

#ifdef _WIN32
    localtime_s(&localTm, &now);
#else
    localtime_r(&now, &localTm);
#endif

    // Avant 6h00, on considère qu'on est encore dans le jour de vote
    // de la veille (le compteur ne "tourne" qu'à partir de 6h00).
    if (localTm.tm_hour < 6)
    {
        now -= 24 * 60 * 60;
#ifdef _WIN32
        localtime_s(&localTm, &now);
#else
        localtime_r(&now, &localTm);
#endif
    }

    char buf[11]; // "YYYY-MM-DD\0"
    strftime(buf, sizeof(buf), "%Y-%m-%d", &localTm);
    return std::string(buf);
}

// ------------------------------------------------------------------
// PlayerScript : suit les fins de donjon/raid en écoutant les kills
// de boss final (référencés dans vote_reward_final_boss, base world).
// ------------------------------------------------------------------
class vote_reward_playerscript : public PlayerScript
{
public:
    vote_reward_playerscript() : PlayerScript("vote_reward_playerscript") { }

    void OnCreatureKill(Player* player, Creature* killed) override
    {
        if (!player || !killed)
            return;

        Map* map = player->GetMap();
        if (!map || (!map->IsDungeon() && !map->IsRaid()))
            return;

        QueryResult result = WorldDatabase.PQuery(
            "SELECT instance_type FROM vote_reward_final_boss WHERE creature_entry = %u",
            killed->GetEntry());

        if (!result)
            return; // pas un boss final référencé, on ignore

        std::string type = (*result)[0].GetString();
        std::string voteDay = GetCurrentVoteDay();
        uint32 accountId = player->GetSession()->GetAccountId();

        if (type == "dungeon")
        {
            CharacterDatabase.PExecute(
                "INSERT INTO account_vote_reward (account_id, vote_day, dungeons_done, raids_done, claimed) "
                "VALUES (%u, '%s', 1, 0, 0) "
                "ON DUPLICATE KEY UPDATE dungeons_done = LEAST(dungeons_done + 1, %u)",
                accountId, voteDay.c_str(), (uint32)VOTE_DUNGEON_GOAL);
        }
        else if (type == "raid")
        {
            CharacterDatabase.PExecute(
                "INSERT INTO account_vote_reward (account_id, vote_day, dungeons_done, raids_done, claimed) "
                "VALUES (%u, '%s', 0, 1, 0) "
                "ON DUPLICATE KEY UPDATE raids_done = LEAST(raids_done + 1, %u)",
                accountId, voteDay.c_str(), (uint32)VOTE_RAID_GOAL);
        }
    }
};

// ------------------------------------------------------------------
// CreatureScript : le PNJ de vote lui-même (gossip).
// ------------------------------------------------------------------
class npc_vote_reward : public CreatureScript
{
public:
    npc_vote_reward() : CreatureScript("npc_vote_reward") { }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        ClearGossipMenuFor(player);
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG,
            "Récupérer mes points de vote quotidiens",
            GOSSIP_SENDER_MAIN, VOTE_ACTION_CLAIM);
        // Deuxième option obligatoire : sur certains clients (Legion+),
        // un menu à option unique est auto-sélectionné sans affichage.
        AddGossipItemFor(player, GOSSIP_ICON_CHAT,
            "Fermer",
            GOSSIP_SENDER_MAIN, VOTE_ACTION_CLOSE);
        SendGossipMenuFor(player, VOTE_GOSSIP_TEXTID, creature->GetGUID());
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* /*creature*/, uint32 /*sender*/, uint32 action) override
    {
        ClearGossipMenuFor(player);

        if (action == VOTE_ACTION_CLAIM)
            HandleClaim(player);

        CloseGossipMenuFor(player);
        return true;
    }

private:
    static void HandleClaim(Player* player)
    {
        uint32 accountId = player->GetSession()->GetAccountId();
        std::string voteDay = GetCurrentVoteDay();

        QueryResult result = CharacterDatabase.PQuery(
            "SELECT dungeons_done, raids_done, claimed FROM account_vote_reward "
            "WHERE account_id = %u AND vote_day = '%s'",
            accountId, voteDay.c_str());

        uint8 dungeonsDone = 0;
        uint8 raidsDone = 0;
        bool alreadyClaimed = false;

        if (result)
        {
            Field* fields = result->Fetch();
            dungeonsDone   = fields[0].GetUInt8();
            raidsDone      = fields[1].GetUInt8();
            alreadyClaimed = fields[2].GetBool();
        }

        if (alreadyClaimed)
        {
            ChatHandler(player->GetSession()).PSendSysMessage(
                "Vous avez déjà récupéré vos points de vote aujourd'hui. Revenez après 6h00.");
            return;
        }

        uint32 points = VOTE_BASE_POINTS;
        bool dungeonGoalReached = dungeonsDone >= VOTE_DUNGEON_GOAL;
        bool raidGoalReached    = raidsDone >= VOTE_RAID_GOAL;

        if (dungeonGoalReached)
            points += VOTE_DUNGEON_BONUS;
        if (raidGoalReached)
            points += VOTE_RAID_BONUS;

        // Marque le jour de vote comme réclamé (crée la ligne si elle
        // n'existait pas encore, ex: joueur n'ayant fait aucun donjon).
        CharacterDatabase.PExecute(
            "INSERT INTO account_vote_reward (account_id, vote_day, dungeons_done, raids_done, claimed) "
            "VALUES (%u, '%s', %u, %u, 1) "
            "ON DUPLICATE KEY UPDATE claimed = 1",
            accountId, voteDay.c_str(), (uint32)dungeonsDone, (uint32)raidsDone);

        // Récupère le battlenet_account lié à ce compte de jeu (auth DB).
        QueryResult bnetResult = LoginDatabase.PQuery(
            "SELECT battlenet_account FROM account WHERE id = %u", accountId);

        if (!bnetResult)
        {
            ChatHandler(player->GetSession()).PSendSysMessage("Erreur : compte introuvable.");
            return;
        }

        uint32 bnetAccountId = (*bnetResult)[0].GetUInt32();
        if (!bnetAccountId)
        {
            ChatHandler(player->GetSession()).PSendSysMessage(
                "Votre compte n'est pas lié à un compte Battle.net, impossible de créditer les points.");
            return;
        }

        LoginDatabase.PExecute(
            "UPDATE battlenet_accounts SET battlePayCredits = battlePayCredits + %u WHERE id = %u",
            points, bnetAccountId);

        ChatHandler(player->GetSession()).PSendSysMessage(
            "Vous avez recu %u points de vote ! (base: %u, objectif donjons: %s, objectif raid: %s)",
            points, (uint32)VOTE_BASE_POINTS,
            dungeonGoalReached ? "atteint" : "non atteint",
            raidGoalReached ? "atteint" : "non atteint");
    }
};

// ------------------------------------------------------------------
// Enregistrement des scripts.
// Appelez AddSC_npc_vote_reward() depuis votre script loader custom
// (voir vote_reward_install.sql / instructions pour le point d'ajout).
// ------------------------------------------------------------------
void AddSC_npc_vote_reward()
{
    new npc_vote_reward();
    new vote_reward_playerscript();
}
