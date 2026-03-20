#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <cocos-ext.h> 
#include <random>

using namespace geode::prelude;

const std::string SERVER_URL = "https://713df0ba-0570-43f8-b018-fb75bbd4baa7-00-1v5ww1splvikp.pike.replit.dev:8080"; 

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
    CCMenuItemSpriteExtra* m_readyBtn;
    CCMenu* m_menu;
    bool m_isAnimating = false;

    // Animation Data Storage
    int m_myHp = 100, m_oppHp = 100;
    int m_myDmg = 0, m_oppDmg = 0;
    int m_myPct = 0, m_oppPct = 0;
    bool m_isGameOver = false;

public:
    static cocos2d::CCScene* scene() {
        auto scene = cocos2d::CCScene::create();
        scene->addChild(DuelMatchLayer::create());
        return scene;
    }

    CREATE_FUNC(DuelMatchLayer);

    bool init() {
        if (!CCLayer::init()) return false;
        
        // GameSoundManager::sharedState()->stopBackgroundMusic(); // Fix chaotic audio
        
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
        m_spectatorLabel->setPosition({winSize.width / 2, winSize.height / 2});
        m_spectatorLabel->setScale(0.8f);
        m_spectatorLabel->setColor({100, 255, 100});
        this->addChild(m_spectatorLabel);

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

        // THE DEATH POPUP: Beautiful and clear feedback
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
        if (m_isAnimating) return; // Completely pause polling during UI sequences
        
        auto req = new CCHttpRequest();
        std::string url = SERVER_URL + "/status?username=" + DuelManager::get()->m_username;
        req->setUrl(url.c_str());
        req->setRequestType(CCHttpRequest::HttpRequestType::kHttpGet);
        req->setResponseCallback(this, httpresponse_selector(DuelMatchLayer::onPollResponse));
        CCHttpClient::getInstance()->send(req);
        req->release();
    }

    void onPollResponse(CCHttpClient*, CCHttpResponse* res) {
        if (!res || !res->isSucceed() || m_isAnimating) return;
        
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
                if (activePlayer == myName) {
                    if (!DuelManager::get()->m_inDuel || DuelManager::get()->m_isDead) {
                        this->unscheduleAllSelectors();
                        startMatch();
                    }
                } else {
                    m_titleLabel->setString("OPPONENT IS PLAYING");
                    std::string bar = "[";
                    int barsToDraw = livePercent / 5; 
                    for(int i = 0; i < 20; i++) bar += (i < barsToDraw) ? "|" : " ";
                    bar += "]";
                    m_spectatorLabel->setString(fmt::format("{}\n\n{}%", bar, livePercent).c_str());
                }
            } 
            else if (status == "calculating" || status == "game_over") {
                if (!m_isAnimating) {
                    m_isAnimating = true; // Set proper boolean flag
                    m_spectatorLabel->setString("");
                    m_titleLabel->setString("");
                    this->unscheduleAllSelectors(); 
                    
                    m_myHp = state[myName]["hp"].asInt().unwrapOr(100);
                    m_oppHp = state[oppName]["hp"].asInt().unwrapOr(100);
                    m_myPct = state[myName]["percent"].asInt().unwrapOr(0);
                    m_oppPct = state[oppName]["percent"].asInt().unwrapOr(0);
                    m_myDmg = state[myName]["lastDamage"].asInt().unwrapOr(0);
                    m_oppDmg = state[oppName]["lastDamage"].asInt().unwrapOr(0);
                    m_isGameOver = (status == "game_over");

                    animatePhase1(); // Start Web-Designer Sequence
                }
            }
        } else {
            m_titleLabel->setString(fmt::format("Players in Queue: {}/2", json["queueCount"].asInt().unwrapOr(1)).c_str());
        }
    }

    // --- PHASE 1: PERCENTAGES ---
    void animatePhase1() {
        auto winSize = CCDirector::sharedDirector()->getWinSize();

        auto myHeader = CCLabelBMFont::create("YOU", "goldFont.fnt");
        auto myPctLabel = CCLabelBMFont::create(fmt::format("{}%", m_myPct).c_str(), "bigFont.fnt");
        myHeader->setPosition({winSize.width * 0.25f, winSize.height * 0.6f});
        myPctLabel->setPosition({winSize.width * 0.25f, winSize.height * 0.45f});
        
        auto oppHeader = CCLabelBMFont::create("OPPONENT", "goldFont.fnt");
        auto oppPctLabel = CCLabelBMFont::create(fmt::format("{}%", m_oppPct).c_str(), "bigFont.fnt");
        oppHeader->setPosition({winSize.width * 0.75f, winSize.height * 0.6f});
        oppPctLabel->setPosition({winSize.width * 0.75f, winSize.height * 0.45f});

        myHeader->setOpacity(0); myPctLabel->setOpacity(0);
        oppHeader->setOpacity(0); oppPctLabel->setOpacity(0);
        this->addChild(myHeader); this->addChild(myPctLabel);
        this->addChild(oppHeader); this->addChild(oppPctLabel);

        myHeader->runAction(CCSequence::create(CCFadeIn::create(0.5f), CCDelayTime::create(1.5f), CCFadeOut::create(0.5f), nullptr));
        myPctLabel->runAction(CCSequence::create(CCFadeIn::create(0.5f), CCDelayTime::create(1.5f), CCFadeOut::create(0.5f), nullptr));
        oppHeader->runAction(CCSequence::create(CCFadeIn::create(0.5f), CCDelayTime::create(1.5f), CCFadeOut::create(0.5f), nullptr));
        
        auto nextPhase = CCCallFunc::create(this, callfunc_selector(DuelMatchLayer::animatePhase2));
        oppPctLabel->runAction(CCSequence::create(CCFadeIn::create(0.5f), CCDelayTime::create(1.5f), CCFadeOut::create(0.5f), nextPhase, nullptr));
    }

    // --- PHASE 2: DAMAGE NUMBERS ---
    void animatePhase2() {
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        
        std::string myDmgStr = m_myDmg > 0 ? fmt::format("-{}", m_myDmg) : "0";
        auto myDmgLabel = CCLabelBMFont::create(myDmgStr.c_str(), "bigFont.fnt");
        myDmgLabel->setPosition({winSize.width * 0.25f, winSize.height * 0.5f});
        myDmgLabel->setColor(m_myDmg > 0 ? cocos2d::ccColor3B{255, 50, 50} : cocos2d::ccColor3B{150, 150, 150});
        
        std::string oppDmgStr = m_oppDmg > 0 ? fmt::format("-{}", m_oppDmg) : "0";
        auto oppDmgLabel = CCLabelBMFont::create(oppDmgStr.c_str(), "bigFont.fnt");
        oppDmgLabel->setPosition({winSize.width * 0.75f, winSize.height * 0.5f});
        oppDmgLabel->setColor(m_oppDmg > 0 ? cocos2d::ccColor3B{255, 50, 50} : cocos2d::ccColor3B{150, 150, 150});

        myDmgLabel->setOpacity(0); oppDmgLabel->setOpacity(0);
        this->addChild(myDmgLabel); this->addChild(oppDmgLabel);

        myDmgLabel->runAction(CCSequence::create(CCFadeIn::create(0.5f), CCDelayTime::create(1.0f), CCFadeOut::create(0.5f), nullptr));
        auto nextPhase = CCCallFunc::create(this, callfunc_selector(DuelMatchLayer::animatePhase3));
        oppDmgLabel->runAction(CCSequence::create(CCFadeIn::create(0.5f), CCDelayTime::create(1.0f), CCFadeOut::create(0.5f), nextPhase, nullptr));
    }

    // --- PHASE 3: HEALTH BARS ---
    void animatePhase3() {
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        m_titleLabel->setString(m_isGameOver ? "MATCH OVER!" : "ROUND RESULTS");

        // YOU
        auto myTitle = CCLabelBMFont::create("YOU", "goldFont.fnt");
        myTitle->setPosition({winSize.width * 0.25f, winSize.height * 0.65f});
        this->addChild(myTitle);

        auto myBarBg = CCSprite::create("square02_001.png");
        myBarBg->setColor({255, 0, 0}); myBarBg->setOpacity(150);
        myBarBg->setPosition({winSize.width * 0.25f, winSize.height * 0.5f});
        myBarBg->setScaleX(2.0f); myBarBg->setScaleY(0.4f);
        this->addChild(myBarBg);

        auto myBarFg = CCSprite::create("square02_001.png");
        myBarFg->setColor({0, 255, 0}); myBarFg->setAnchorPoint({0.0f, 0.5f}); 
        myBarFg->setPosition({winSize.width * 0.25f - (myBarBg->getScaledContentSize().width / 2), winSize.height * 0.5f});
        myBarFg->setScaleY(0.4f);
        
        float startMyHp = (m_myHp + m_myDmg) / 100.0f * 2.0f;
        float endMyHp = (m_myHp) / 100.0f * 2.0f;
        myBarFg->setScaleX(startMyHp);
        this->addChild(myBarFg);
        myBarFg->runAction(CCScaleTo::create(1.0f, endMyHp, 0.4f));

        // OPPONENT
        auto oppTitle = CCLabelBMFont::create("OPPONENT", "goldFont.fnt");
        oppTitle->setPosition({winSize.width * 0.75f, winSize.height * 0.65f});
        this->addChild(oppTitle);

        auto oppBarBg = CCSprite::create("square02_001.png");
        oppBarBg->setColor({255, 0, 0}); oppBarBg->setOpacity(150);
        oppBarBg->setPosition({winSize.width * 0.75f, winSize.height * 0.5f});
        oppBarBg->setScaleX(2.0f); oppBarBg->setScaleY(0.4f);
        this->addChild(oppBarBg);

        auto oppBarFg = CCSprite::create("square02_001.png");
        oppBarFg->setColor({0, 255, 0}); oppBarFg->setAnchorPoint({0.0f, 0.5f});
        oppBarFg->setPosition({winSize.width * 0.75f - (oppBarBg->getScaledContentSize().width / 2), winSize.height * 0.5f});
        oppBarFg->setScaleY(0.4f);
        
        float startOppHp = (m_oppHp + m_oppDmg) / 100.0f * 2.0f;
        float endOppHp = (m_oppHp) / 100.0f * 2.0f;
        oppBarFg->setScaleX(startOppHp);
        this->addChild(oppBarFg);
        oppBarFg->runAction(CCScaleTo::create(1.0f, endOppHp, 0.4f));

        this->scheduleOnce(schedule_selector(DuelMatchLayer::showReadyButton), 1.5f);
    }

    void showReadyButton(float dt) {
        m_readyBtn->setVisible(true);
        m_isAnimating = false; // Animation fully complete
        this->schedule(schedule_selector(DuelMatchLayer::pollServer), 1.0f);
    }

    void startMatch() {
        DuelManager::get()->m_inDuel = true;
        DuelManager::get()->m_isDead = false;
        auto level = GameLevelManager::sharedState()->getMainLevel(22, false);
        if (level) {
            CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.5f, PlayLayer::scene(level, false, false)));
        }
    }

    void onReadyClick(CCObject*) {
        m_readyBtn->setVisible(false);
        m_titleLabel->setString("Waiting for opponent...");
        auto req = new CCHttpRequest();
        req->setUrl((SERVER_URL + "/ready").c_str());
        req->setRequestType(CCHttpRequest::HttpRequestType::kHttpPost);
        std::string body = "{\"matchId\":\"" + DuelManager::get()->m_matchId + "\", \"username\":\"" + DuelManager::get()->m_username + "\"}";
        req->setRequestData(body.c_str(), body.length());
        CCHttpClient::getInstance()->send(req);
        req->release();
    }

    void onExitClick(CCObject*) {
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

class $modify(MyPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
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
            if (percent == 0) return; 

            DuelManager::get()->m_isDead = true;
            DuelManager::get()->m_lastPercent = percent;
            DuelManager::get()->m_justDied = true; // Flags the popup for the menu
            this->unschedule(schedule_selector(MyPlayLayer::syncLiveProgress)); 
            
            auto req = new CCHttpRequest();
            req->setUrl((SERVER_URL + "/die").c_str());
            req->setRequestType(CCHttpRequest::HttpRequestType::kHttpPost);
            std::string body = "{\"matchId\":\"" + DuelManager::get()->m_matchId + "\", \"username\":\"" + DuelManager::get()->m_username + "\", \"percent\":" + std::to_string(percent) + "}";
            req->setRequestData(body.c_str(), body.length());
            CCHttpClient::getInstance()->send(req);
            req->release();
            // We intentionally do NOT schedule a manual kick here anymore. 
            // The resetLevel hook handles it cleanly.
        }
    }

    // THE KICK DELAY BUG FIX: Intercept the level reset and warp to the menu instead!
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
            // Just let the normal transition handle it, or exit manually
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
