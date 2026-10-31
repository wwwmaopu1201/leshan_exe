#import <AppKit/AppKit.h>
#include "../server/src/ui_dialogs.h"
#include <QWidget>

static NSWindow *nativeWindow(QWidget *widget) {
    if(!widget) return nil;
    NSView *view=reinterpret_cast<NSView *>(widget->winId());
    return [view window];
}

bool uiMacOwnerClickThrough(QWidget *widget) {
    NSWindow *window=nativeWindow(widget);
    return window ? [window ignoresMouseEvents] : false;
}

bool uiMacOwnerBlocked(QWidget *widget) {
    NSWindow *window=nativeWindow(widget);
    return window && ![window isMovable] && ![window ignoresMouseEvents];
}

UiModalNativeState uiMacBlockOwnerWindow(QWidget *widget) {
    UiModalNativeState state;
    NSWindow *window=nativeWindow(widget);
    if(!window) return state;
    state.valid=true;
    state.ignoresMouseEvents=[window ignoresMouseEvents];
    state.movable=[window isMovable];
    NSButton *close=[window standardWindowButton:NSWindowCloseButton];
    NSButton *minimize=[window standardWindowButton:NSWindowMiniaturizeButton];
    NSButton *zoom=[window standardWindowButton:NSWindowZoomButton];
    if(close) state.closeEnabled=[close isEnabled];
    if(minimize) state.minimizeEnabled=[minimize isEnabled];
    if(zoom) state.zoomEnabled=[zoom isEnabled];
    [window setIgnoresMouseEvents:NO];
    [window setMovable:NO];
    if(close) [close setEnabled:NO];
    if(minimize) [minimize setEnabled:NO];
    if(zoom) [zoom setEnabled:NO];
    return state;
}

void uiMacRestoreOwnerWindow(QWidget *widget,const UiModalNativeState &state) {
    if(!state.valid) return;
    NSWindow *window=nativeWindow(widget);
    if(!window) return;
    [window setMovable:state.movable];
    [window setIgnoresMouseEvents:state.ignoresMouseEvents];
    NSButton *close=[window standardWindowButton:NSWindowCloseButton];
    NSButton *minimize=[window standardWindowButton:NSWindowMiniaturizeButton];
    NSButton *zoom=[window standardWindowButton:NSWindowZoomButton];
    if(close) [close setEnabled:state.closeEnabled];
    if(minimize) [minimize setEnabled:state.minimizeEnabled];
    if(zoom) [zoom setEnabled:state.zoomEnabled];
}
