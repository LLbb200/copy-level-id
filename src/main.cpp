#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <cocos-ext.h> 
#include <random> // Add this at the top with the other includes

using namespace geode::prelude;
using namespace cocos2d::extension;

// --- PUT YOUR REPLIT URL HERE ---
const std::string SERVER_URL = "https://713df0ba-0570-43f8-b018-fb75bbd4baa7-00-1v5ww1splvikp.pike.replit.dev:8080"; 

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
        // Only generate this ONCE per session so we don't lose our ID
        if (!m_username.empty()) return;

        auto am = GJAccountManager::sharedState();
        std::string baseName = am->m_username;
        
        if (baseName.empty()) {
            baseName = "Player";
        }

        // Generate a random 4-digit number to make this device unique
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> distr(1000, 9999);
        
        m_username = baseName + "#" + std::to_string(distr(gen));
        
        log::info("Assigned unique Session ID: {}", m_username);
    }
};

// --- THE MATCHMAKING & VERSUS LAYER ---
class DuelMatchLayer : public cocos2d::CCLayer {
protected:
    CCLabelBMFont* m_statusLabel;
    CCLabelBMFont* m_vsLabel;
    CCMenuItemSpriteExtra* m_readyBtn;
    CCMenu* m_menu;

public:
    static cocos2d::CCScene* scene() {
        auto scene = cocos2d::CCScene::create();
        auto layer = DuelMatchLayer::create();
        scene->addChild(layer);
        return scene;
    }

    CREATE_FUNC(DuelMatchLayer);

    bool init() {
        if (!CCLayer::init()) return false;
        
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        
        // Background
        auto bg = CCSprite::create("GJ_gradientBG.png");
        bg->setPosition(winSize / 2);
        bg->setScaleX(winSize.width / bg->getContentSize().width);
        bg->setScaleY(winSize.height / bg->getContentSize().height);
        bg->setColor({30, 30, 60}); // Dark blue theme
        this->addChild(bg);

        m_menu = CCMenu::create();
        m_menu->setPosition({0, 0});
        this->addChild(m_menu);

        m_statusLabel = CCLabelBMFont::create("Connecting...", "goldFont.fnt");
        m_statusLabel->setPosition({winSize.width / 2, winSize.height - 50});
        this->addChild(m_statusLabel);

        m_vsLabel = CCLabelBMFont::create("", "bigFont.fnt");
        m_vsLabel->setPosition({winSize.width / 2, winSize.height / 2});
        m_vsLabel->setScale(0.7f);
        this->addChild(m_vsLabel);

        // Cancel / Exit Button
        auto exitBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Exit / Cancel"), this, menu_selector(DuelMatchLayer::onExitClick)
        );
        exitBtn->setPosition({winSize.width / 2, 40});
        m_menu->addChild(exitBtn);

        // Ready Button (Hidden initially)
        m_readyBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Next Round"), this, menu_selector(DuelMatchLayer::onReadyClick)
        );
        m_readyBtn->setPosition({winSize.width / 2, 100});
        m_readyBtn->setVisible(false);
        m_menu->addChild(m_readyBtn);

        DuelManager::get()->fetchUsername();

        if (DuelManager::get()->m_inDuel) {
            // We are returning from a death
            m_statusLabel->setString("Waiting for Opponent...");
            this->schedule(schedule_selector(DuelMatchLayer::pollServer), 1.0f);
        } else {
            // We are joining the queue
            joinQueue();
        }

        return true;
    }

    void joinQueue() {
        m_statusLabel->setString("Joining Queue...");
        auto req = new CCHttpRequest();
        req->setUrl((SERVER_URL + "/queue").c_str());
        req->setRequestType(CCHttpRequest::HttpRequestType::kHttpPost);
        std::string body = "{\"username\":\"" + DuelManager::get()->m_username + "\"}";
        req->setRequestData(body.c_str(), body.length());
        
        req->setResponseCallback(this, httpresponse_selector(DuelMatchLayer::onQueueJoined));
        req->setHeaders({"Content-Type: application/json"});
        CCHttpClient::getInstance()->send(req);
        req->release();
    }

    void onQueueJoined(CCHttpClient*, CCHttpResponse* res) {
        if (!res || !res->isSucceed()) {
            m_statusLabel->setString("Server Offline!");
            return;
        }
        m_statusLabel->setString("Searching for opponent...");
        this->schedule(schedule_selector(DuelMatchLayer::pollServer), 1.5f);
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

            if (status == "playing") {
                if (!DuelManager::get()->m_inDuel || DuelManager::get()->m_isDead) {
                    this->unscheduleAllSelectors();
                    startMatch();
                }
            } 
            else if (status == "calculating" || status == "game_over") {
                // Opponent died, show results!
                auto state = data["state"];
                std::string p1 = data["players"][0].asString().unwrapOr("P1");
                std::string p2 = data["players"][1].asString().unwrapOr("P2");
                
                int hp1 = state[p1]["hp"].asInt().unwrapOr(100);
                int hp2 = state[p2]["hp"].asInt().unwrapOr(100);
                
                m_statusLabel->setString(status == "game_over" ? "MATCH OVER" : "ROUND OVER");
                m_vsLabel->setString(fmt::format("{} ({} HP)\nVS\n{} ({} HP)", p1, hp1, p2, hp2).c_str());
                
                if (status == "calculating") m_readyBtn->setVisible(true);
            }
        } else {
            int count = json["queueCount"].asInt().unwrapOr(1);
            m_statusLabel->setString(fmt::format("Searching... Players in Queue: {}/2", count).c_str());
        }
    }

    void startMatch() {
        DuelManager::get()->m_inDuel = true;
        DuelManager::get()->m_isDead = false;
        
        // Load Stereo Madness (Level 22)
        auto GLM = GameLevelManager::sharedState();
        auto level = GLM->getMainLevel(22, false);
        if (level) {
            auto scene = PlayLayer::scene(level, false, false);
            CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.5f, scene));
        }
    }

    void onReadyClick(CCObject*) {
        m_readyBtn->setVisible(false);
        m_statusLabel->setString("Waiting for opponent to ready up...");
        
        auto req = new CCHttpRequest();
        req->setUrl((SERVER_URL + "/ready").c_str());
        req->setRequestType(CCHttpRequest::HttpRequestType::kHttpPost);
        std::string body = "{\"matchId\":\"" + DuelManager::get()->m_matchId + "\", \"username\":\"" + DuelManager::get()->m_username + "\"}";
        req->setRequestData(body.c_str(), body.length());
        req->setHeaders({"Content-Type: application/json"});
        CCHttpClient::getInstance()->send(req);
        req->release();
    }

    void onExitClick(CCObject*) {
        this->unscheduleAllSelectors();
        DuelManager::get()->m_inDuel = false;
        
        // Leave Queue
        auto req = new CCHttpRequest();
        req->setUrl((SERVER_URL + "/leave").c_str());
        req->setRequestType(CCHttpRequest::HttpRequestType::kHttpPost);
        std::string body = "{\"username\":\"" + DuelManager::get()->m_username + "\"}";
        req->setRequestData(body.c_str(), body.length());
        req->setHeaders({"Content-Type: application/json"});
        CCHttpClient::getInstance()->send(req);
        req->release();

        auto menuScene = MenuLayer::scene(false);
        CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.5f, menuScene));
    }
};

// --- MENU LAYER HOOK (ENTRY POINT) ---
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

// --- PLAY LAYER HOOK (ANTI-CHEESE & DEATH) ---
class $modify(MyPlayLayer, PlayLayer) {
    
    // ANTI CHEESE: Disable pausing during a duel
    void pauseGame(bool p0) {
        if (DuelManager::get()->m_inDuel) {
            // Do nothing! They cannot pause.
            return; 
        }
        PlayLayer::pauseGame(p0);
    }

    void destroyPlayer(PlayerObject* p, GameObject* g) {
        PlayLayer::destroyPlayer(p, g);

        if (DuelManager::get()->m_inDuel && !DuelManager::get()->m_isDead) {
            DuelManager::get()->m_isDead = true;
            int percent = this->getCurrentPercentInt();
            
            // Send Death to Server
            auto req = new CCHttpRequest();
            req->setUrl((SERVER_URL + "/die").c_str());
            req->setRequestType(CCHttpRequest::HttpRequestType::kHttpPost);
            std::string body = "{\"matchId\":\"" + DuelManager::get()->m_matchId + "\", \"username\":\"" + DuelManager::get()->m_username + "\", \"percent\":" + std::to_string(percent) + "}";
            req->setRequestData(body.c_str(), body.length());
            req->setHeaders({"Content-Type: application/json"});
            CCHttpClient::getInstance()->send(req);
            req->release();

            // Kick back to Versus layer after 1.5 seconds (lets the death animation play)
            this->scheduleOnce(schedule_selector(MyPlayLayer::kickToVersus), 1.5f);
        }
    }

    void kickToVersus(float dt) {
        CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.5f, DuelMatchLayer::scene()));
    }
    
    // If they beat the level
    void levelComplete() {
        PlayLayer::levelComplete();
        if (DuelManager::get()->m_inDuel && !DuelManager::get()->m_isDead) {
            DuelManager::get()->m_isDead = true;
            
            auto req = new CCHttpRequest();
            req->setUrl((SERVER_URL + "/die").c_str());
            req->setRequestType(CCHttpRequest::HttpRequestType::kHttpPost);
            std::string body = "{\"matchId\":\"" + DuelManager::get()->m_matchId + "\", \"username\":\"" + DuelManager::get()->m_username + "\", \"percent\": 100 }";
            req->setRequestData(body.c_str(), body.length());
            req->setHeaders({"Content-Type: application/json"});
            CCHttpClient::getInstance()->send(req);
            req->release();

            this->scheduleOnce(schedule_selector(MyPlayLayer::kickToVersus), 2.0f);
        }
    }
};
