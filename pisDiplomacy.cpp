 /*
"map.json" format:
```
{
  "territoryName": {
    "part_name (_L for land, _C for coast)": ["neighbor", "neighbor"],
    "center": "0 for not, 1 for yes",
    "initPlayer": "name of the initPlayer, None if there is no initPlayer",
    "initPart": "name of the part that is initialized with a unit None if no initial unit"
  }
}
```
e.g.
```
{
  "LON": {
    "LON_L": ["YOR", "WAL"],
    "LON_C": ["YOR", "WAL", "ENG", "NTH"],
    "center": 1,
    "initPlayer": "ENG",
    "initPart": "LON_C"
  },
  "STP": {
    "STP_L": ["FIN", "MOS", "LVN"],
    "STP_NC": ["BAR", "NWY"],
    "STP_SC": ["FIN", "BOT", "LVN"],
    "center": 1,
    "initPlayer": "RUS",
    "initPart": "STP_SC"
  },
  "UKR": {
    "UKR_L": ["MOS", "WAR", "SEV", "GAL", "RUM"],
    "center": 0,
    "initPlayer": RUS,
    "initPart": "None"
  },
  "BEL": {
    "BEL_L": ["HOL", "PIC", "RUH", "BUR"],
    "BEL_C": ["NTH", "ENG"],
    "center": 1,
    "initPlayer": None,
    "initPart": None,
}
```

"rules.json" format:
```
{
  "winCondition": "the number of centers one player has to control to win",
  "buildRule": "initCenters or allCenters",
  "buildTime": "how many phases once buildPhase",
  "voteShown": "0 for not, 1 for yes",
  "drawType": "DSS, equal split for draws, or SoS, weighted split on draw",
}
```
e.g.
```
{
  "winCondition": 18,
  "buildRule": "initCenters",
  "buildTime": 4,
  "voteShown": 1,
  "drawType": DSS
}
```

"log.json" format ($ prefix indicates variables, () indicates comments, log at end of every phase):
```
{
  "Phase $phaseCount $phaseType (move/build/retreat)": {
    "$playerName": [
      "$partName M (move)/S (support hold)/V (via convoy)/R (retreat) $partName",
      "$partName H (hold)/B (build)/D (disband)",
      "$partName S (support move)/C (convoy) $partName from $partName"
    ]
  }
}
```
e.g.
```
{
  "Phase 1 move": {
    "ENG": [
      "LON_C M NTH_C",
      "EDI_C M NWG_C",
      "LVP_L M YOR_L"
    ]
  }
}
```

Order input format (std input):
`diplomacy --order $playerName $partName M (move)/S (support hold)/V (via convoy)/R (retreat) to $partName`
`diplomacy --order $playerName H (hold)/B (build)/D (disband) $partName`
`diplomacy --order $playerName $partName S (support move)/C (convoy) to $partName from $partName`

Draw vote input format (std input):
`diplomacy --draw 1`
1 for voting draw, 0 for cancelling draw

Press input format (std input):
`diplomacy --press $playerName $playerName $message`
`diplomacy --press $playerName public $message`
send from first playerName to second playerName

Map output format (std output, output at end of every phase or if asked with `diplomacy --map`):
output the map JSON file

Rules output format (std output, output if asked with `diplomacy --rules`):
output the rules JSON file

Phase output format (std output, output at the start of every phase or if asked with `diplomacy --phase`):
All phase:
`Phase $phaseCount $phaseType`
Build phase:
`$playerName build/disband $n`
Retreat phase:
`$playerName retreat $partName (, $partName2)`

Press output format (std output, output if asked with `diplomacy --press $playerName/public`):
`$playerName/public: $message`
*/

#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class Part {
public:
    std::string name;
    std::vector<Part*> neighbors;
    Territory* belonged;
    unsigned char LC; // 0 for land, 1 for coast
    Player* unit;
};

class Territory {
public:
    std::string name;
    std::vector<std::unique_ptr<Part>> parts;
    unsigned char center; // 0 for not, 1 for yes
    Player* owner;
};

class Player {
public:
    std::string name;
    std::vector<Territory*> allowBuild;
    int centerCount;
    int unitCount;
    std::vector<Part*> units;
    bool vote;
    bool ready;
};

class Game {
private:
    std::vector<std::unique_ptr<Territory>> allTerritories;
    std::vector<std::unique_ptr<Player>> allPlayers;
    std::vector<std::tuple<std::weak_ptr<Player>,std::weak_ptr<Player>,std::string>> press; // allPlayer[0] for public, with name "public"
    uint winCondition;
    unsigned char buildRule; // 0 for initTerritories, 1 for allTerritories
    uint buildTime;
    bool voteShown;
    unsigned char drawType; // 0 for DSS, 1for SoS
    uint phaseCount; // Retreat phase not counted
    std::string log;
    std::string logFilePath;
    std::string mapRaw;
    std::string rulesRaw;
    void movePhase();
    void retreatPhase();
    void buildPhase();
    void checkVotes();

public:
    Game(const std::string& mapPath, const std::string& rulesPath);
    void initialize();
    void play();
};

Game::Game(const std::string& mapPath, const std::string& rulesPath) {
    std::ifstream mapFile(mapPath);
    std::ifstream rulesFile(rulesPath);
    
    if (!mapFile || !rulesFile) {
        throw std::runtime_error("Failed to open input files");
    }
    
    json mapJson = json::parse(mapFile);
    json rulesJson = json::parse(rulesFile);
    
    mapRaw = mapJson.dump();
    rulesRaw = rulesJson.dump();
    
    winCondition = rulesJson["winCondition"];
    buildRule = (rulesJson["buildRule"] == "allCenters") ? 1 : 0;
    buildTime = rulesJson["buildTime"];
    voteShown = rulesJson["voteShown"] == 1;
    drawType = (rulesJson["drawType"] == "DSS") ? 0 : 1;
    phaseCount = 1;
    logFilePath = "log.json";
    
    for (auto& [territoryName, territoryData] : mapJson.items()) {
        auto territory = std::make_unique<Territory>();
        territory->name = territoryName;
        territory->center = territoryData["center"];
        territory->owner = nullptr;
        
        for (auto& [partName, neighbors] : territoryData.items()) {
            if (partName != "center" && partName != "initPlayer" && partName != "initPart") {
                auto part = std::make_unique<Part>();
                part->name = partName;
                part->belonged = territory.get();
                part->unit = nullptr;
                part->LC = (partName.substr(partName.size()-1)=='C') ? 1 : 0;
                territory->parts.push_back(std::move(part));
            }
        }
        
        allTerritories.push_back(std::move(territory));
    }
    
    std::unordered_map<std::string, Player*> playerMap;
    auto publicPlayer = std::make_unique<Player>();
    publicPlayer->name = "public";
    publicPlayer->centerCount = 0;
    publicPlayer->unitCount = 0;
    publicPlayer->vote = true;
    publicPlayer->ready = true;
    allPlayers.push_back(std::move(publicPlayer));
    for (auto& [territoryName, territoryData] : mapJson.items()) {
        if (!territoryData["initPlayer"].is_null()) {
            std::string playerName = territoryData["initPlayer"];
            if (playerMap.find(playerName) == playerMap.end()) {
                auto player = std::make_unique<Player>();
                player->name = playerName;
                player->centerCount = 0;
                player->unitCount = 0;
                player->vote = false;
                player->ready = false;
                playerMap[playerName] = player.get();
                allPlayers.push_back(std::move(player));
            }
        }
    }
}

void Game::initialize() {
    json mapJson = json::parse(mapRaw);
    
    for (auto& territory : allTerritories) {
        auto& territoryData = mapJson[territory->name];
        if (!territoryData["initPlayer"].is_null()) {
            std::string playerName = territoryData["initPlayer"];
            std::string initPartName = territoryData["initPart"];
            
            auto playerIt = std::find_if(allPlayers.begin(), allPlayers.end(),
                [&playerName](const auto& p) { return p->name == playerName; });
            
            if (playerIt != allPlayers.end()) {
                Player* player = playerIt->get();
                
                auto partIt = std::find_if(territory->parts.begin(), territory->parts.end(),
                    [&initPartName](const auto& p) { return p->name == initPartName; });
                
                if (partIt != territory->parts.end()) {
                    Part* part = partIt->get();
                    part->unit = player;
                    player->units.push_back(part);
                    player->unitCount++;
                }
                
                if (territory->center) {
                    territory->owner = player;
                    player->centerCount++;
                    player->allowBuild.push_back(territory.get());
                }
            }
        }
    }
}

int main() {
    try {
        Game diplomacy("map.json", "rules.json");
        diplomacy.initialize();
        diplomacy.play();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}