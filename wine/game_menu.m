// Optional Cocoa menu bridge for the pinned macOS Wine runtime.
// The PE host polls actions on its UI thread; Cocoa never calls Windows code.
#import <Cocoa/Cocoa.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>

static _Atomic(uint32_t) state;
static atomic_bool active, scheduled;
static NSMenuItem *gameMenu;

@interface AppxGameMenuTarget : NSObject
- (void)openGameMenu:(id)sender;
@end
@implementation AppxGameMenuTarget
- (void)openGameMenu:(id)sender {
  (void)sender;
  atomic_fetch_or(&state, 2);
}
@end
static AppxGameMenuTarget *target;

static void scheduleInstall(void) {
  if (!atomic_load(&active) || (atomic_load(&state) & 1) || atomic_exchange(&scheduled, true)) return;
  dispatch_async(dispatch_get_main_queue(), ^{
    if (!atomic_load(&active) || gameMenu || !NSApp.mainMenu) {
      atomic_store(&scheduled, false);
      return;
    }
    target = [AppxGameMenuTarget new];
    gameMenu = [[NSMenuItem alloc] initWithTitle:@"Game Menu"
                                          action:NULL
                                   keyEquivalent:@""];
    NSMenu *menu = [[NSMenu alloc] initWithTitle:@"Game Menu"];
    NSMenuItem *open =
        [[NSMenuItem alloc] initWithTitle:@"Game settings…"
                                   action:@selector(openGameMenu:)
                            keyEquivalent:@""];
    open.target = target;
    [menu addItem:open];
    gameMenu.submenu = menu;
    [NSApp.mainMenu insertItem:gameMenu
                       atIndex:MIN(1, NSApp.mainMenu.numberOfItems)];
    atomic_store(&state, 1);
    atomic_store(&scheduled, false);
  });
}

static int32_t start(void *args) {
  if (!args || *(uint32_t *)args != 1) return (int32_t)0xc000000d;
  atomic_store(&active, true);
  scheduleInstall();
  return 0;
}

static int32_t poll(void *args) {
  if (!args)
    return (int32_t)0xc000000d;
  // Wine creates its menu when the first window becomes visible.
  scheduleInstall();
  *(uint32_t *)args = atomic_fetch_and(&state, ~2u);
  return 0;
}

static int32_t stop(void *args) {
  (void)args;
  atomic_store(&active, false);
  dispatch_async(dispatch_get_main_queue(), ^{
    [gameMenu.menu removeItem:gameMenu];
    gameMenu = nil;
    target = nil;
    atomic_store(&state, 0);
  });
  return 0;
}

// All arguments contain uint32_t only, so the ABI is identical for WoW64.
__attribute__((visibility("default")))
int32_t (*const __wine_unix_call_funcs[])(void *) = {
  start,
  poll,
  stop
};
__attribute__((visibility("default")))
int32_t (*const __wine_unix_call_wow64_funcs[])(void *) = {
  start,
  poll,
  stop
};
