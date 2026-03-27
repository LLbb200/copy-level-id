#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <cocos-ext.h> 
#include <random>

using namespace geode::prelude;

const std::string SERVER_URL = "https://YOUR-REPLIT-URL-HERE"; 

class DuelManager {
public:
    std::string m_username = "";
    std::string m_matchId = "";
    bool m_inDuel = false;
    bool m_isDead = false;
    bool m_justDied = false; 
    int m_lastPercent = 0;

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

class DuelMatchLayer : public cocos2d::CCLayer {
protected:
    CCLabelBMFont* m_titleLabel;
    CCLabelBMFont* m_spectatorLabel;
    CCLabelBMFont* m_resultsLabel; // New: To show the instant numbers
    CCMenuItemSpriteExtra* m_readyBtn;
    CCMenu* m_menu;
    bool m_showingResults = false;

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

        m_titleLabel = CCLabelBMFont::create("Connecting...", "goldFont.fnt");
        m_titleLabel->setPosition({winSize.width / 2, winSize.height - 40});
        this->addChild(m_titleLabel);

        m_spectatorLabel = CCLabelBMFont::create("", "bigFont.fnt");
        m_spectatorLabel->setPosition({winSize.width / 2, winSize.height / 2 + 30});
        m_spectatorLabel->setScale(0.8f);
        m_spectatorLabel->setColor({100, 255, 100});
        this->addChild(m_spectatorLabel);

        // Label for instant health/damage text
        m_resultsLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_resultsLabel->setPosition({winSize.width / 2, winSize.height / 2 - 20});
        m_resultsLabel->setScale(1.2f);
        this->addChild(m_resultsLabel);

        m_menu = CCMenu::create();
        m_menu->setPosition({0, 0});
        this->addChild(m_menu);

        auto exitBtn = CCMenuItemSpriteExtra::create(ButtonSprite::create("Leave Match"), this, menu_selector(DuelMatchLayer::onExitClick));
        exitBtn->setPosition({winSize.width / 2, 30});
        m_menu->addChild(exitBtn);

        m_readyBtn = CCMenuItemSpriteExtra::create(ButtonSprite::create("Ready for Next Round"), this, menu_selector(DuelMatchLayer::onReadyClick));
        m_readyBtn->setPosition({winSize.width / 2, 80});
        m_readyBtn->setVisible(false);
        m_menu->addChild(m_readyBtn);

        DuelManager::get()->fetchUsername();

        if (DuelManager::get()->m_justDied) {
            DuelManager::get()->m_justDied = false;
            FLAlertLayer::create("Duel Update", fmt::format("You died at <cy>{}%</c>!", DuelManager::get()->m_lastPercent).c_str(), "OK")->show();
        }

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
        if (!res || !res->isSucceed()) { m_titleLabel->setString("Server Offline!"); return; }
        m_titleLabel->setString("Searching for opponent...");
        this->schedule(schedule_selector(DuelMatchLayer::pollServer), 0.5f);
    }

    void pollServer(float dt) {
        // Stop polling if we are currently looking at a match result screen
        if (m_showingResults) return; 
        
        auto req = new CCHttpRequest();
        std::string url = SERVER_URL + "/status?username=" + DuelManager::get()->m_username;
        req->setUrl(url.c_str());
        req->setRequestType(CCHttpRequest::HttpRequestType::kHttpGet);
        req->setResponseCallback(this, httpresponse_selector(DuelMatchLayer::onPollResponse));
        CCHttpClient::getInstance()->send(req);
        req->release();
    }

    void onPollResponse(CCHttpClient*, CCHttpResponse* res) {
        if (!res || !res->isSucceed() || m_showingResults) return;
        
        std::vector<char>* buffer = res->getResponseData();
        std::string resStr(buffer->begin(), buffer->end());
        auto json = matjson::parse(resStr).unwrapOr(matjson::makeObject({}));
        
        if (json["matchFound"].asBool().unwrapOr(false)) {
            auto data = json["matchData"];
            DuelManager::get()->m_matchId = json["matchId"].asString().unwrapOr("");
            std::string status = data["status"].asString().unwrapOr("");
            std::string activePlayer = data["activePlayer"].asString().unwrapOr("");

            std::string myName = DuelManager::get()->m_username;
            std::string oppName = (data["players"][0].asString().unwrapOr("") == myName) ? 
                                   data["players"][1].asString().unwrapOr("") : 
                                   data["players"][0].asString().unwrapOr("");
            auto state = data["state"];

            if (status == "playing") {
                if (activePlayer == myName) {
                    if (!DuelManager::get()->m_inDuel || DuelManager::get()->m_isDead) {
                        this->unscheduleAllSelectors();
                        startMatch();
                    }
                } else {
                    m_titleLabel->setString("OPPONENT IS PLAYING");
                    
                    // Grab spectator data from state
                    int oppPercent = state[oppName]["percent"].asInt().unwrapOr(0);
                    m_spectatorLabel->setString(fmt::format("Opponent Progress: {}%", oppPercent).c_str());
                }
            } 
            else if (status == "calculating" || status == "game_over") {
                if (!m_showingResults) {
                    m_showingResults = true;
                    this->unscheduleAllSelectors(); 
                    
                    int myHp = state[myName]["hp"].asInt().unwrapOr(100);
                    int oppHp = state[oppName]["hp"].asInt().unwrapOr(100);
                    int myPct = state[myName]["percent"].asInt().unwrapOr(0);
                    int oppPct = state[oppName]["percent"].asInt().unwrapOr(0);
                    int myDmg = state[myName]["lastDamage"].asInt().unwrapOr(0);
                    int oppDmg = state[oppName]["lastDamage"].asInt().unwrapOr(0);

                    showNumbers(status == "game_over", myHp, oppHp, myPct, oppPct, myDmg, oppDmg);
                }
            }
        } else {
            m_titleLabel->setString(fmt::format("Players in Queue: {}/2", json["queueCount"].asInt().unwrapOr(1)).c_str());
        }
    }

    // --- INSTANT NUMBERS INSTEAD OF ANIMATIONS ---
    void showNumbers(bool isGameOver, int myHp, int oppHp, int myPct, int oppPct, int myDmg, int oppDmg) {
        m_titleLabel->setString(isGameOver ? "MATCH OVER!" : "ROUND RESULTS");
        m_spectatorLabel->setString("");

        std::string results = fmt::format(
            "YOU:\nPercent: {}%\nDamage Taken: -{}\nRemaining HP: {}\n\nOPPONENT:\nPercent: {}%\nDamage Taken: -{}\nRemaining HP: {}",
            myPct, myDmg, myHp, oppPct, oppDmg, oppHp
        );

        m_resultsLabel->setString(results.c_str());
        m_readyBtn->setVisible(!isGameOver);
    }

    void startMatch() {
        DuelManager::get()->m_inDuel = true;
        DuelManager::get()->m_isDead = false;
        m_showingResults = false;
        
        // Ensure you change '22' to whichever level ID you are testing on
        auto level = GameLevelManager::sharedState()->getMainLevel(22, false); 
        if (level) {
            CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.5f, PlayLayer::scene(level, false, false)));
        }
    }

    void onReadyClick(CCObject*) {
        m_readyBtn->setVisible(false);
        m_titleLabel->setString("Waiting for opponent...");
        m_resultsLabel->setString("");
        
        auto req = new CCHttpRequest();
        req->setUrl((SERVER_URL + "/ready").c_str());
        req->setRequestType(CCHttpRequest::HttpRequestType::kHttpPost);
        std::string body = "{\"matchId\":\"" + DuelManager::get()->m_matchId + "\", \"username\":\"" + DuelManager::get()->m_username + "\"}";
        req->setRequestData(body.c_str(), body.length());
        CCHttpClient::getInstance()->send(req);
        req->release();
        
        m_showingResults = false;
        this->schedule(schedule_selector(DuelMatchLayer::pollServer), 1.0f);
    }

    void onExitClick(CCObject*) {
        this->unscheduleAllSelectors();
        DuelManager::get()->m_inDuel = false;
        
        // Note: Make sure your node server has a /leave endpoint configured if you keep this
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

class $modify(MyPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        if (DuelManager::get()->m_inDuel) {
            // Speed up the polling slightly to lay groundwork for spectating
            this->schedule(schedule_selector(MyPlayLayer::syncLiveProgress), 0.1f); 
        }
        return true;
    }

    void syncLiveProgress(float dt) {
        if (!DuelManager::get()->m_inDuel || DuelManager::get()->m_isDead) return;
        if (!this->m_player1) return;

        auto req = new CCHttpRequest();
        // Pointing this to your server's sync_pos endpoint
        req->setUrl((SERVER_URL + "/sync_pos").c_str()); 
        req->setRequestType(CCHttpRequest::HttpRequestType::kHttpPost);
        
        // Grab real player values so the server gets populated for the future ghost spectator!
        float x = this->m_player1->getPositionX();
        float y = this->m_player1->getPositionY();
        float rot = this->m_player1->getRotation();
        int pct = this->getCurrentPercentInt();

        std::string body = fmt::format(
            "{{\"matchId\":\"{}\", \"username\":\"{}\", \"x\":{}, \"y\":{}, \"rot\":{}, \"percent\":{}}}",
            DuelManager::get()->m_matchId, DuelManager::get()->m_username, x, y, rot, pct
        );

        req->setRequestData(body.c_str(), body.length());
        CCHttpClient::getInstance()->send(req);
        req->release();
    }

    void destroyPlayer(PlayerObject* p, GameObject* g) {
        PlayLayer::destroyPlayer(p, g);

        if (DuelManager::get()->m_inDuel && !DuelManager::get()->m_isDead) {
            int percent = this->getCurrentPercentInt();
            if (percent == 0) return; 

            DuelManager::get()->m_isDead = true;
            DuelManager::get()->m_lastPercent = percent;
            DuelManager::get()->m_justDied = true; 
            this->unschedule(schedule_selector(MyPlayLayer::syncLiveProgress)); 
            
            auto req = new CCHttpRequest();
            req->setUrl((SERVER_URL + "/die").c_str());
            req->setRequestType(CCHttpRequest::HttpRequestType::kHttpPost);
            std::string body = "{\"matchId\":\"" + DuelManager::get()->m_matchId + "\", \"username\":\"" + DuelManager::get()->m_username + "\", \"percent\":" + std::to_string(percent) + "}";
            req->setRequestData(body.c_str(), body.length());
            CCHttpClient::getInstance()->send(req);
            req->release();
        }
    }

    void resetLevel() {
        if (DuelManager::get()->m_inDuel && DuelManager::get()->m_isDead) {
            CCDirector::sharedDirector()->replaceScene(DuelMatchLayer::scene());
            return;
        }
        PlayLayer::resetLevel();
    }

    void pauseGame(bool p0) {
        if (DuelManager::get()->m_inDuel) return; 
        PlayLayer::pauseGame(p0);
    }
    
    void levelComplete() {
        PlayLayer::levelComplete();
        if (DuelManager::get()->m_inDuel && !DuelManager::get()->m_isDead) {
            DuelManager::get()->m_isDead = true;
            DuelManager::get()->m_lastPercent = 100;
            DuelManager::get()->m_justDied = true;
            
            auto req = new CCHttpRequest();
            req->setUrl((SERVER_URL + "/die").c_str());
            req->setRequestType(CCHttpRequest::HttpRequestType::kHttpPost);
            std::string body = "{\"matchId\":\"" + DuelManager::get()->m_matchId + "\", \"username\":\"" + DuelManager::get()->m_username + "\", \"percent\": 100 }";
            req->setRequestData(body.c_str(), body.length());
            CCHttpClient::getInstance()->send(req);
            req->release();
        }
    }
};

class $modify(MyMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        auto menu = this->getChildByID("bottom-menu");
        auto btn = CCMenuItemSpriteExtra::create(ButtonSprite::create("Duel Matchmaking"), this, menu_selector(MyMenuLayer::onStartDuel));
        menu->addChild(btn);
        menu->updateLayout();
        return true;
    }
    void onStartDuel(CCObject*) {
        CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.5f, DuelMatchLayer::scene()));
    }
};
