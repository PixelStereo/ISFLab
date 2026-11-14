#import <AppKit/AppKit.h>

void applyMacApplicationIcon() {
    @autoreleasepool {
        NSBundle *bundle = [NSBundle mainBundle];
        NSString *name = [bundle objectForInfoDictionaryKey:@"CFBundleIconFile"];
        if (!name) return;
        NSString *path = [[bundle resourcePath] stringByAppendingPathComponent:name];
        NSImage *icon = [[NSImage alloc] initWithContentsOfFile:path];
        if (icon) [[NSApplication sharedApplication] setApplicationIconImage:icon];
        [icon release];
    }
}

// Background helper processes (the shader check worker) must not show a Dock icon or take focus.
void hideMacApplicationFromDock() {
    @autoreleasepool {
        [[NSApplication sharedApplication] setActivationPolicy:NSApplicationActivationPolicyProhibited];
    }
}
