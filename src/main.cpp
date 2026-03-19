#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <cocos-ext.h> 
#include <random>

using namespace geode::prelude;
using namespace cocos2d::extension;

const std::string SERVER_URL = "https://YOUR-REPLIT-URL-HERE"; 

class DuelManager {
public:
    std::string m_username = "";
    std::string m_matchId = "";
    bool m_inDuel = false;
    bool m_isDead = false;

    static DuelManager* get() {
        static DuelManager instance;
        return &instance;
    }

    void fetchUsername() {
        if (!m_username.empty()) return;
        auto am = GJAccountManager::sharedState();
        std::string baseName = am->m_username;
        if (baseName.empty()) baseName = "Player";
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> distr(1000, 9999);
        m_username = baseName + "_" + std::to_string(distr(gen));
    }
};

// --- THE MATCHMAKING & VERSUS LAYER ---
class DuelMatchLayer : public cocos2d::CCLayer {
protected:
    CCLabelBMFont* m_titleLabel;
    CCLabelBMFont* m_spectatorLabel;
    CCLabelBMFont* m_resultsLabel;
    CCMenuItemSpriteExtra* m_readyBtn;
    CCMenu* m_menu;

public:
    static cocos2d::CCScene* scene() {
        auto scene = cocos2d::CCScene::create();
        scene->addChild(DuelMatchLayer::create());
        return scene;
    }

    CREATE_FUNC(DuelMatchLayer);

    bool init() {
        if (!CCLayer::init()) return false;
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        
        auto bg = CCSprite::create("GJ_gradientBG.png");
        bg->setPosition(winSize / 2);
        bg->setScaleX(winSize.width / bg->getContentSize().width);
        bg->setScaleY(winSize.height / bg->getContentSize().height);
        bg->setColor({20, 20, 40}); 
        this->addChild(bg);

        // CLEAN UI LAYOUT
        m_titleLabel = CCLabelBMFont::create("Connecting...", "goldFont.fnt");
        m_titleLabel->setPosition({winSize.width / 2, winSize.height - 40});
        this->addChild(m_titleLabel);

        m_spectatorLabel = CCLabelBMFont::create("", "bigFont.fnt");
        m_spectatorLabel->setPosition({winSize.width / 2, winSize.height / 2});
        m_spectatorLabel->setScale(0.8f);
        m_spectatorLabel->setColor({100, 255, 100}); // Green for spectate progress
        this->addChild(m_spectatorLabel);

        m_resultsLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_resultsLabel->setPosition({winSize.width / 2, winSize.height / 2});
        m_resultsLabel->setScale(1.5f);
        this->addChild(m_resultsLabel);

        m_menu = CCMenu::create();
        m_menu->setPosition({0, 0});
        this->addChild(m_menu);

        auto exitBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Leave Match"), this, menu_selector(DuelMatchLayer::onExitClick)
        );
        exitBtn->setPosition({winSize.width / 2, 40});
        m_menu->addChild(exitBtn);

        m_readyBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Ready for Next Round"), this, menu_selector(DuelMatchLayer::onReadyClick)
        );
        m_readyBtn->setPosition({winSize.width / 2, 90});
        m_readyBtn->setVisible(false);
        m_menu->addChild(m_readyBtn);

        DuelManager::get()->fetchUsername();

        if (DuelManager::get()->m_inDuel) {
            m_titleLabel->setString("Waiting for Server...");
            this->schedule(schedule_selector(DuelMatchLayer::pollServer), 0.5f);
        } else {
            joinQueue();
        }
        return true;
    }

    void joinQueue() {
        m_titleLabel->setString("Joining Queue...");
        auto req = new CCHttpRequest();
        req->setUrl((SERVER_URL + "/queue").c_str());
        req->setRequestType(CCHttpRequest::HttpRequestType::kHttpPost);
        std::string body = "{\"username\":\"" + DuelManager::get()->m_username + "\"}";
        req->setRequestData(body.c_str(), body.length());
        req->setResponseCallback(this, httpresponse_selector(DuelMatchLayer::onQueueJoined));
        CCHttpClient::getInstance()->send(req);
        req->release();
    }

    void onQueueJoined(CCHttpClient*, CCHttpResponse* res) {
        if (!res || !res->isSucceed()) {
            m_titleLabel->setString("Server Offline!");
            return;
        }
        m_titleLabel->setString("Searching for opponent...");
        this->schedule(schedule_selector(DuelMatchLayer::pollServer), 0.5f); // Fast polling for snappy turns
    }

    void pollServer(float dt) {
        auto req = new CCHttpRequest();
        std::string url = SERVER_URL + "/status?username=" + DuelManager::get()->m_username;
        req->setUrl(url.c_str());
        req->setRequestType(CCHttpRequest::HttpRequestType::kHttpGet);
        req->setResponseCallback(this, httpresponse_selector(DuelMatchLayer::onPollResponse));
        CCHttpClient::getInstance()->send(req);
        req->release();
    }

    void onPollResponse(CCHttpClient*, CCHttpResponse* res) {
        if (!res || !res->isSucceed()) return;
        
        std::vector<char>* buffer = res->getResponseData();
        std::string resStr(buffer->begin(), buffer->end());
        auto json = matjson::parse(resStr).unwrapOr(matjson::makeObject({}));
        
        if (json["matchFound"].asBool().unwrapOr(false)) {
            auto data = json["matchData"];
            DuelManager::get()->m_matchId = json["matchId"].asString().unwrapOr("");
            std::string status = data["status"].asString().unwrapOr("");
            std::string activePlayer = data["activePlayer"].asString().unwrapOr("");
            int livePercent = data["livePercent"].asInt().unwrapOr(0);

            std::string myName = DuelManager::get()->m_username;
            std::string oppName = (data["players"][0].asString().unwrapOr("") == myName) ? 
                                   data["players"][1].asString().unwrapOr("") : 
                                   data["players"][0].asString().unwrapOr("");
            auto state = data["state"];

            if (status == "playing") {
                m_resultsLabel->setString(""); // Clear results text

                if (activePlayer == myName) {
                    if (!DuelManager::get()->m_inDuel || DuelManager::get()->m_isDead) {
                        this->unscheduleAllSelectors();
                        startMatch(); // IT IS OUR TURN!
                    }
                } else {
                    // SPECTATING MODE!
                    m_titleLabel->setString("SPECTATING");
                    m_spectatorLabel->setString(fmt::format("{} is playing...\nLive Progress: {}%", oppName, livePercent).c_str());
                }
            } 
            else if (status == "calculating" || status == "game_over") {
                // RESULTS SCREEN
                m_spectatorLabel->setString(""); // Clear spectate text
                m_titleLabel->setString(status == "game_over" ? "MATCH OVER!" : "ROUND OVER!");
                
                int myHp = state[myName]["hp"].asInt().unwrapOr(100);
                int oppHp = state[oppName]["hp"].asInt().unwrapOr(100);
                int myPct = state[myName]["percent"].asInt().unwrapOr(0);
                int oppPct = state[oppName]["percent"].asInt().unwrapOr(0);
                int myDmg = state[myName]["lastDamage"].asInt().unwrapOr(0);
                int oppDmg = state[oppName]["lastDamage"].asInt().unwrapOr(0);

                std::string text = fmt::format(
                    "You ({}): {}% ({} HP)\nDamage Taken: {}\n\n-----------------\n\nOpponent ({}): {}% ({} HP)\nDamage Taken: {}",
                    myName, myPct, myHp, myDmg,
                    oppName, oppPct, oppHp, oppDmg
                );
                
                m_resultsLabel->setString(text.c_str());
                if (status == "calculating") m_readyBtn->setVisible(true);
            }
        } else {
            int count = json["queueCount"].asInt().unwrapOr(1);
            m_titleLabel->setString(fmt::format("Searching... Players in Queue: {}/2", count).c_str());
        }
    }

    void startMatch() {
        DuelManager::get()->m_inDuel = true;
        DuelManager::get()->m_isDead = false;
        auto GLM = GameLevelManager::sharedState();
        auto level = GLM->getMainLevel(22, false);
        if (level) {
            auto scene = PlayLayer::scene(level, false, false);
            CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.5f, scene));
        }
    }

    void onReadyClick(CCObject*) {
        m_readyBtn->setVisible(false);
        m_resultsLabel->setString("Waiting for opponent...");
        auto req = new CCHttpRequest();
        req->setUrl((SERVER_URL + "/ready").c_str());
        req->setRequestType(CCHttpRequest::HttpRequestType::kHttpPost);
        std::string body = "{\"matchId\":\"" + DuelManager::get()->m_matchId + "\", \"username\":\"" + DuelManager::get()->m_username + "\"}";
        req->setRequestData(body.c_str(), body.length());
        CCHttpClient::getInstance()->send(req);
        req->release();
    }

    void onExitClick(CCObject*) {
        // Exit logic remains identical
        this->unscheduleAllSelectors();
        DuelManager::get()->m_inDuel = false;
        auto req = new CCHttpRequest();
        req->setUrl((SERVER_URL + "/leave").c_str());
        req->setRequestType(CCHttpRequest::HttpRequestType::kHttpPost);
        std::string body = "{\"username\":\"" + DuelManager::get()->m_username + "\"}";
        req->setRequestData(body.c_str(), body.length());
        CCHttpClient::getInstance()->send(req);
        req->release();
        CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.5f, MenuLayer::scene(false)));
    }
};

// --- PLAY LAYER HOOK ---
class $modify(MyPlayLayer, PlayLayer) {
    
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        
        // If in a duel, schedule a live percentage sync every 0.5 seconds
        if (DuelManager::get()->m_inDuel) {
            this->schedule(schedule_selector(MyPlayLayer::syncLiveProgress), 0.5f);
        }
        return true;
    }

    void syncLiveProgress(float dt) {
        if (!DuelManager::get()->m_inDuel || DuelManager::get()->m_isDead) return;
        
        auto req = new CCHttpRequest();
        req->setUrl((SERVER_URL + "/sync").c_str());
        req->setRequestType(CCHttpRequest::HttpRequestType::kHttpPost);
        std::string body = "{\"matchId\":\"" + DuelManager::get()->m_matchId + "\", \"username\":\"" + DuelManager::get()->m_username + "\", \"percent\":" + std::to_string(this->getCurrentPercentInt()) + "}";
        req->setRequestData(body.c_str(), body.length());
        CCHttpClient::getInstance()->send(req);
        req->release();
    }

    void destroyPlayer(PlayerObject* p, GameObject* g) {
        PlayLayer::destroyPlayer(p, g);

        if (DuelManager::get()->m_inDuel && !DuelManager::get()->m_isDead) {
            int percent = this->getCurrentPercentInt();
            
            // THE 0% BUG FIX: Ignore deaths at 0%. Let them respawn naturally.
            if (percent == 0) return; 

            DuelManager::get()->m_isDead = true;
            this->unschedule(schedule_selector(MyPlayLayer::syncLiveProgress)); // Stop syncing
            
            auto req = new CCHttpRequest();
            req->setUrl((SERVER_URL + "/die").c_str());
            req->setRequestType(CCHttpRequest::HttpRequestType::kHttpPost);
            std::string body = "{\"matchId\":\"" + DuelManager::get()->m_matchId + "\", \"username\":\"" + DuelManager::get()->m_username + "\", \"percent\":" + std::to_string(percent) + "}";
            req->setRequestData(body.c_str(), body.length());
            CCHttpClient::getInstance()->send(req);
            req->release();

            this->scheduleOnce(schedule_selector(MyPlayLayer::kickToVersus), 1.5f);
        }
    }

    void pauseGame(bool p0) {
        if (DuelManager::get()->m_inDuel) return; // Anti-cheese
        PlayLayer::pauseGame(p0);
    }

    void kickToVersus(float dt) {
        CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.5f, DuelMatchLayer::scene()));
    }
    
    void levelComplete() {
        PlayLayer::levelComplete();
        if (DuelManager::get()->m_inDuel && !DuelManager::get()->m_isDead) {
            DuelManager::get()->m_isDead = true;
            auto req = new CCHttpRequest();
            req->setUrl((SERVER_URL + "/die").c_str());
            req->setRequestType(CCHttpRequest::HttpRequestType::kHttpPost);
            std::string body = "{\"matchId\":\"" + DuelManager::get()->m_matchId + "\", \"username\":\"" + DuelManager::get()->m_username + "\", \"percent\": 100 }";
            req->setRequestData(body.c_str(), body.length());
            CCHttpClient::getInstance()->send(req);
            req->release();
            this->scheduleOnce(schedule_selector(MyPlayLayer::kickToVersus), 2.0f);
        }
    }
};

class $modify(MyMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        auto menu = this->getChildByID("bottom-menu");
        auto btn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Duel Matchmaking"), this, menu_selector(MyMenuLayer::onStartDuel)
        );
        menu->addChild(btn);
        menu->updateLayout();
        return true;
    }
    void onStartDuel(CCObject*) {
        CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.5f, DuelMatchLayer::scene()));
    }
};
