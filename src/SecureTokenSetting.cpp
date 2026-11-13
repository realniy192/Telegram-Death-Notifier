#include "SecureTokenSetting.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/TextInput.hpp>

using namespace geode::prelude;

namespace {
constexpr char kTokenChars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:_-";

class MaskedTokenSetting : public SettingBaseValueV3<std::string, std::string const&> {
public:
    static Result<std::shared_ptr<SettingV3>> parse(std::string key, std::string modID,
                                                  matjson::Value const& json) {
        auto setting = std::make_shared<MaskedTokenSetting>();
        auto parsed = setting->parseBaseProperties(std::move(key), std::move(modID), json);
        if (!parsed) return Err(parsed.unwrapErr());
        return Ok(std::static_pointer_cast<SettingV3>(setting));
    }

    Result<> isValid(std::string const& value) const override {
        if (value.find_first_not_of(kTokenChars) != std::string::npos) {
            return Err("Use a valid Telegram bot token (letters, digits, : _ -).");
        }
        return Ok();
    }

    SettingNodeV3* createNode(float width) override;
};

class MaskedTokenNode : public SettingValueNodeV3<MaskedTokenSetting> {
    TextInput* m_input = nullptr;

    bool init(std::shared_ptr<MaskedTokenSetting> setting, float width) {
        if (!SettingValueNodeV3::init(setting, width)) return false;
        float controlsWidth = getButtonMenu()->getContentWidth();
        m_input = TextInput::create(std::max(50.f, (controlsWidth - 40.f) / .7f), "Token");
        m_input->setPasswordMode(true);
        m_input->setFilter(kTokenChars);
        m_input->setScale(.7f);
        m_input->setString(getValue());
        m_input->setCallback([this](std::string const& value) { setValue(value, m_input); });
        m_input->setPosition({ (controlsWidth - 40.f) / 2, 15.f });
        getButtonMenu()->addChild(m_input);

        auto sprite = ButtonSprite::create("Clear");
        sprite->setScale(.45f);
        auto clear = CCMenuItemSpriteExtra::create(sprite, this, menu_selector(MaskedTokenNode::onClear));
        clear->setPosition({ controlsWidth - 17.f, 15.f });
        getButtonMenu()->addChild(clear);
        updateState(nullptr);
        return true;
    }

    void updateState(CCNode* invoker) override {
        SettingValueNodeV3::updateState(invoker);
        if (!m_input) return;
        if (invoker != m_input) m_input->setString(getValue());
        m_input->setEnabled(getSetting()->shouldEnable());
    }

public:
    void onClear(CCObject*) {
        m_input->defocus();
        setValue("", nullptr);
    }

    static MaskedTokenNode* create(std::shared_ptr<MaskedTokenSetting> setting, float width) {
        auto node = new MaskedTokenNode();
        if (node->init(std::move(setting), width)) {
            node->autorelease();
            return node;
        }
        delete node;
        return nullptr;
    }
};

SettingNodeV3* MaskedTokenSetting::createNode(float width) {
    return MaskedTokenNode::create(std::static_pointer_cast<MaskedTokenSetting>(shared_from_this()), width);
}

$execute {
    auto result = Mod::get()->registerCustomSettingType("masked-token", MaskedTokenSetting::parse);
    if (!result) log::error("Unable to register masked token setting");
}
}

std::string tg::botToken() {
    auto setting = Mod::get()->getSetting("bot-token");
    if (auto masked = std::dynamic_pointer_cast<MaskedTokenSetting>(setting)) return masked->getValue();
    return Mod::get()->getSettingValue<std::string>("bot-token");
}

void tg::setBotToken(std::string const& value) {
    auto setting = Mod::get()->getSetting("bot-token");
    if (auto masked = std::dynamic_pointer_cast<MaskedTokenSetting>(setting)) masked->setValue(value);
    else Mod::get()->setSettingValue<std::string>("bot-token", value);
}
