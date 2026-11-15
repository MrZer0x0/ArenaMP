#include "windowbase.hpp"

#include <algorithm>
#include <vector>

#include <MyGUI_Button.h>
#include <MyGUI_ILayer.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_RenderManager.h>

#include <components/settings/settings.hpp>

#include "../mwbase/windowmanager.hpp"
#include "../mwbase/environment.hpp"

#include <components/widgets/imagebutton.hpp>

#include "draganddrop.hpp"
#include "exposedwindow.hpp"

using namespace MWGui;

WindowBase::WindowBase(const std::string& parLayout)
  : Layout(parLayout)
{
    mMainWidget->setVisible(false);

    Window* window = mMainWidget->castType<Window>(false);
    if (!window)
        return;

    MyGUI::Button* button = nullptr;
    MyGUI::VectorWidgetPtr widgets = window->getSkinWidgetsByName("Action");
    for (MyGUI::Widget* widget : widgets)
    {
        if (widget->isUserString("SupportDoubleClick"))
            button = widget->castType<MyGUI::Button>();
    }

    if (button)
        button->eventMouseButtonDoubleClick += MyGUI::newDelegate(this, &WindowBase::onDoubleClick);
}

void WindowBase::onTitleDoubleClicked()
{
    if (MyGUI::InputManager::getInstance().isShiftPressed())
        MWBase::Environment::get().getWindowManager()->toggleMaximized(this);
}

void WindowBase::onDoubleClick(MyGUI::Widget *_sender)
{
    onTitleDoubleClicked();
}

namespace
{
    // ArenaMP UI9: windows fade in when they open and out when they close.
    //
    // A closing window is *logically* hidden at once: isVisible() is false,
    // onClose() has run and it takes no input (enabled flag cleared silently,
    // so its skin does not switch to the disabled look). Only the picture
    // stays for the fade-out, then the widget is really hidden and its alpha
    // restored. Reopening during a fade simply reverses it.
    struct WindowFade
    {
        MyGUI::Widget* mWidget = nullptr;
        float mTarget = 1.f;  ///< the window's own alpha, restored afterwards
        bool mClosing = false;
    };

    std::vector<WindowFade> sFades;
    constexpr float sFadeInTime = 0.14f;
    constexpr float sFadeOutTime = 0.11f;

    std::vector<WindowFade>::iterator findFade(MyGUI::Widget* widget)
    {
        return std::find_if(sFades.begin(), sFades.end(),
            [widget](const WindowFade& fade) { return fade.mWidget == widget; });
    }

    bool fadesEnabled(MyGUI::Widget* widget, bool modal)
    {
        if (widget == nullptr || widget->getLayer() == nullptr || modal)
            return false;
        // UI9 fix: only ordinary gameplay windows on the "Windows" layer
        // (inventory, map, stats, magic, containers, trade, ...). The dialogue,
        // message boxes, popups, journal and every modal window open and close
        // instantly: their input handling (modal grabs, deferred message boxes,
        // dialogue focus) relies on the widget being hidden at once.
        const std::string& layer = widget->getLayer()->getName();
        if (layer != "Windows")
            return false;
        try
        {
            return Settings::Manager::getBool("arenamp window fade", "GUI");
        }
        catch (const std::exception&)
        {
            return true;
        }
    }
}

WindowBase::~WindowBase()
{
    // A window may be deleted while it fades (message boxes, ArenaMP dialogs).
    auto it = findFade(mMainWidget);
    if (it != sFades.end())
        sFades.erase(it);
}

void WindowBase::setVisible(bool visible)
{
    bool wasVisible = isVisible();

    auto fade = findFade(mMainWidget);
    const bool fading = fade != sFades.end();
    if (!fadesEnabled(mMainWidget, dynamic_cast<WindowModal*>(this) != nullptr))
    {
        if (fading)
        {
            mMainWidget->setAlpha(fade->mTarget);
            mMainWidget->setEnabledSilent(true);
            sFades.erase(fade);
        }
        mMainWidget->setVisible(visible);
    }
    else if (visible)
    {
        float target = mMainWidget->getAlpha();
        if (fading)
        {
            target = fade->mTarget;
            fade->mClosing = false;
        }
        mMainWidget->setEnabledSilent(true);
        if (!mMainWidget->getVisible())
        {
            mMainWidget->setAlpha(0.f);
            mMainWidget->setVisible(true);
        }
        if (!fading && mMainWidget->getAlpha() < target)
            sFades.push_back({ mMainWidget, target, false });
    }
    else if (wasVisible)
    {
        if (fading)
            fade->mClosing = true;
        else
            sFades.push_back({ mMainWidget, mMainWidget->getAlpha(), true });
        mMainWidget->setEnabledSilent(false);
    }

    if (visible)
        onOpen();
    else if (wasVisible)
        onClose();
}

bool WindowBase::isVisible()
{
    if (!mMainWidget->getVisible())
        return false;
    auto fade = findFade(mMainWidget);
    return fade == sFades.end() || !fade->mClosing;
}

void WindowBase::updateFades(float duration)
{
    duration = std::max(0.f, std::min(duration, 0.1f));
    for (auto it = sFades.begin(); it != sFades.end();)
    {
        MyGUI::Widget* widget = it->mWidget;
        float alpha = widget->getAlpha();
        bool done = false;
        if (it->mClosing)
        {
            alpha -= duration * std::max(0.01f, it->mTarget) / sFadeOutTime;
            if (alpha <= 0.f)
            {
                widget->setVisible(false);
                widget->setAlpha(it->mTarget);
                widget->setEnabledSilent(true);
                done = true;
            }
            else
                widget->setAlpha(alpha);
        }
        else
        {
            alpha += duration * std::max(0.01f, it->mTarget) / sFadeInTime;
            if (alpha >= it->mTarget)
            {
                widget->setAlpha(it->mTarget);
                done = true;
            }
            else
                widget->setAlpha(alpha);
        }
        it = done ? sFades.erase(it) : it + 1;
    }
}

void WindowBase::finishFades()
{
    for (const WindowFade& fade : sFades)
    {
        if (fade.mClosing)
            fade.mWidget->setVisible(false);
        fade.mWidget->setAlpha(fade.mTarget);
        fade.mWidget->setEnabledSilent(true);
    }
    sFades.clear();
}

void WindowBase::center()
{
    // Centre dialog

    MyGUI::IntSize layerSize = MyGUI::RenderManager::getInstance().getViewSize();
    if (mMainWidget->getLayer())
        layerSize = mMainWidget->getLayer()->getSize();

    MyGUI::IntCoord coord = mMainWidget->getCoord();
    coord.left = (layerSize.width - coord.width)/2;
    coord.top = (layerSize.height - coord.height)/2;
    mMainWidget->setCoord(coord);
}

WindowModal::WindowModal(const std::string& parLayout)
    : WindowBase(parLayout)
{
}

void WindowModal::onOpen()
{
    MWBase::Environment::get().getWindowManager()->addCurrentModal(this); //Set so we can escape it if needed

    MyGUI::Widget* focus = MyGUI::InputManager::getInstance().getKeyFocusWidget();
    MyGUI::InputManager::getInstance ().addWidgetModal (mMainWidget);
    MyGUI::InputManager::getInstance().setKeyFocusWidget(focus);
}

void WindowModal::onClose()
{
    MWBase::Environment::get().getWindowManager()->removeCurrentModal(this);

    MyGUI::InputManager::getInstance ().removeWidgetModal (mMainWidget);
}

NoDrop::NoDrop(DragAndDrop *drag, MyGUI::Widget *widget)
    : mWidget(widget), mDrag(drag), mTransparent(false)
{
}

void NoDrop::onFrame(float dt)
{
    if (!mWidget)
        return;

    MyGUI::IntPoint mousePos = MyGUI::InputManager::getInstance().getMousePosition();

    if (mDrag->mIsOnDragAndDrop)
    {
        MyGUI::Widget* focus = MyGUI::InputManager::getInstance().getMouseFocusWidget();
        while (focus && focus != mWidget)
            focus = focus->getParent();

        if (focus == mWidget)
            mTransparent = true;
    }
    if (!mWidget->getAbsoluteCoord().inside(mousePos))
        mTransparent = false;

    if (mTransparent)
    {
        mWidget->setNeedMouseFocus(false); // Allow click-through
        setAlpha(std::max(0.13f, mWidget->getAlpha() - dt*5));
    }
    else
    {
        mWidget->setNeedMouseFocus(true);
        setAlpha(std::min(1.0f, mWidget->getAlpha() + dt*5));
    }
}

void NoDrop::setAlpha(float alpha)
{
    if (mWidget)
        mWidget->setAlpha(alpha);
}

BookWindowBase::BookWindowBase(const std::string& parLayout)
  : WindowBase(parLayout)
{
}

float BookWindowBase::adjustButton (char const * name)
{
    Gui::ImageButton* button;
    WindowBase::getWidget (button, name);
    MyGUI::IntSize requested = button->getRequestedSize();
    float scale = float(requested.height) / button->getSize().height;
    MyGUI::IntSize newSize = requested;
    newSize.width /= scale;
    newSize.height /= scale;
    button->setSize(newSize);

    if (button->getAlign().isRight())
    {
        MyGUI::IntSize diff = (button->getSize() - requested);
        diff.width /= scale;
        diff.height /= scale;
        button->setPosition(button->getPosition() + MyGUI::IntPoint(diff.width,0));
    }

    return scale;
}
