/* Sign in with Apple (engine/platform/google_auth.h, provider "apple").
 * Native AuthenticationServices sheet; the request carries sha256(nonce)
 * and the raw nonce goes to Supabase with the identity token. Apple sends
 * the user's name only on the first authorisation, so it is passed on
 * with the result and saved at once. Compiled with ARC. */
#import <AuthenticationServices/AuthenticationServices.h>
#import <CommonCrypto/CommonDigest.h>
#import <Security/Security.h>
#import <UIKit/UIKit.h>

#include <SDL3/SDL.h>

#include "platform/google_auth_internal.h"

@interface MGAppleSignIn : NSObject <ASAuthorizationControllerDelegate,
                                     ASAuthorizationControllerPresentationContextProviding>
@property(nonatomic, weak) UIWindow *window;
@property(nonatomic, strong) ASAuthorizationController *controller;
@property(nonatomic, copy) NSString *rawNonce;
@property(nonatomic, assign) GoogleAuth *result;
@end

static void finish(GoogleAuth *g, NSString *token, NSString *nonce, NSString *error)
{
    google_auth_finish(g, token ? token.UTF8String : NULL, nonce ? nonce.UTF8String : NULL,
                       error ? error.UTF8String : NULL);
    if (g->platform) {
        CFBridgingRelease(g->platform);
        g->platform = NULL;
    }
}

@implementation MGAppleSignIn
- (ASPresentationAnchor)presentationAnchorForAuthorizationController:(ASAuthorizationController *)controller
{
    (void)controller;
    return self.window;
}

- (void)authorizationController:(ASAuthorizationController *)controller
    didCompleteWithAuthorization:(ASAuthorization *)authorization
{
    (void)controller;
    ASAuthorizationAppleIDCredential *cred = (ASAuthorizationAppleIDCredential *)authorization.credential;
    if (![cred isKindOfClass:[ASAuthorizationAppleIDCredential class]] || !cred.identityToken) {
        finish(self.result, nil, nil, @"no identity token");
        return;
    }
    NSString *token = [[NSString alloc] initWithData:cred.identityToken encoding:NSUTF8StringEncoding];
    NSPersonNameComponents *n = cred.fullName;
    if (n.givenName || n.familyName) {
        NSString *full = [NSString stringWithFormat:@"%@%@%@", n.givenName ?: @"",
                                                   n.givenName && n.familyName ? @" " : @"", n.familyName ?: @""];
        native_auth_set_name(self.result, full.UTF8String);
    }
    finish(self.result, token, self.rawNonce, nil);
}

- (void)authorizationController:(ASAuthorizationController *)controller didCompleteWithError:(NSError *)error
{
    (void)controller;
    finish(self.result, nil, nil, error.code == ASAuthorizationErrorCanceled ? @"cancelled" : error.localizedDescription);
}
@end

static NSString *hex_sha256(NSString *s)
{
    NSData *in = [s dataUsingEncoding:NSUTF8StringEncoding];
    unsigned char out[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(in.bytes, (CC_LONG)in.length, out);
    NSMutableString *h = [NSMutableString stringWithCapacity:64];
    for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; i++) {
        [h appendFormat:@"%02x", out[i]];
    }
    return h;
}

static NSString *random_hex(size_t bytes)
{
    NSMutableData *d = [NSMutableData dataWithLength:bytes];
    if (SecRandomCopyBytes(kSecRandomDefault, bytes, d.mutableBytes) != errSecSuccess) {
        arc4random_buf(d.mutableBytes, bytes);
    }
    NSMutableString *h = [NSMutableString stringWithCapacity:bytes * 2];
    const unsigned char *b = d.bytes;
    for (size_t i = 0; i < bytes; i++) {
        [h appendFormat:@"%02x", b[i]];
    }
    return h;
}

bool apple_auth_platform_available(const GoogleAuth *g)
{
    (void)g;
#ifdef MG_APPLE_SIGNIN /* entitlement present (CMake MG_IOS_APPLE_SIGNIN) */
    return true;
#else
    return false;
#endif
}

bool apple_auth_platform_start(GoogleAuth *g, struct SDL_Window *window)
{
    UIWindow *win = (__bridge UIWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                                                 SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, NULL);
    MGAppleSignIn *flow = [MGAppleSignIn new];
    flow.window = win;
    flow.result = g;
    flow.rawNonce = random_hex(32);
    ASAuthorizationAppleIDRequest *req = [[ASAuthorizationAppleIDProvider new] createRequest];
    req.requestedScopes = @[ ASAuthorizationScopeFullName, ASAuthorizationScopeEmail ];
    req.nonce = hex_sha256(flow.rawNonce);
    flow.controller = [[ASAuthorizationController alloc] initWithAuthorizationRequests:@[ req ]];
    flow.controller.delegate = flow;
    flow.controller.presentationContextProvider = flow;
    g->platform = (void *)CFBridgingRetain(flow); /* alive until finish() */
    [flow.controller performRequests];
    return true;
}
