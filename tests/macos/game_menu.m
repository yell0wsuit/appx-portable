#import <Cocoa/Cocoa.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>

typedef int32_t (*Entry)(void *);
static BOOL menuUnavailable;
@interface AppxMenuTestApplication : NSApplication
@end
@implementation AppxMenuTestApplication
- (NSMenu *)mainMenu { return menuUnavailable ? nil : [super mainMenu]; }
@end
static void drain(void) {
    [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
}
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL: %s\n", #condition); return 1; } } while (0)

int main(int argc, char **argv) {
    @autoreleasepool {
        CHECK(argc == 2);
        [AppxMenuTestApplication sharedApplication];
        NSMenu *main = [NSMenu new];
        NSMenuItem *app = [NSMenuItem new];
        NSMenu *appMenu = [NSMenu new];
        NSMenuItem *preferences = [[NSMenuItem alloc] initWithTitle:@"Preferences…" action:NULL keyEquivalent:@","];
        [appMenu addItem:preferences];
        app.submenu = appMenu;
        [main addItem:app];
        NSApp.mainMenu = main;
        menuUnavailable = YES;
        void *library = dlopen(argv[1], RTLD_NOW);
        if (!library) fprintf(stderr, "%s\n", dlerror());
        CHECK(library);
        Entry *entries = dlsym(library, "__wine_unix_call_funcs");
        CHECK(entries);
        uint32_t version = 1, state = 0;
        CHECK(entries[0](&version) == 0);
        drain();
        CHECK(entries[1](&state) == 0 && state == 0);
        menuUnavailable = NO;
        CHECK(entries[1](&state) == 0);
        drain();
        CHECK(main.numberOfItems == 2);
        NSMenuItem *game = [main itemWithTitle:@"Game Menu"];
        CHECK(game && game.submenu.numberOfItems == 1);
        NSMenuItem *open = [game.submenu itemAtIndex:0];
        CHECK(open.keyEquivalent.length == 0);
        CHECK([preferences.keyEquivalent isEqualToString:@","]);
        CHECK(entries[1](&state) == 0 && state == 1);
        CHECK([NSApp sendAction:open.action to:open.target from:open]);
        CHECK(entries[1](&state) == 0 && state == 3);
        CHECK(entries[1](&state) == 0 && state == 1);
        CHECK(entries[0](&version) == 0);
        drain();
        CHECK(main.numberOfItems == 2);
        CHECK(entries[2](NULL) == 0);
        drain();
        CHECK(main.numberOfItems == 1);
        CHECK(entries[1](&state) == 0 && state == 0);
        puts("PASS: native Game Menu lifecycle, action dispatch, and Command+, preservation");
    }
    return 0;
}
