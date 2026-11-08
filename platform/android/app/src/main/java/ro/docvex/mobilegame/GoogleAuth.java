package ro.docvex.mobilegame;

import android.app.Activity;
import android.os.CancellationSignal;

import androidx.credentials.Credential;
import androidx.credentials.CredentialManager;
import androidx.credentials.CredentialManagerCallback;
import androidx.credentials.CustomCredential;
import androidx.credentials.GetCredentialRequest;
import androidx.credentials.GetCredentialResponse;
import androidx.credentials.exceptions.GetCredentialCancellationException;
import androidx.credentials.exceptions.GetCredentialException;

import com.google.android.libraries.identity.googleid.GetGoogleIdOption;
import com.google.android.libraries.identity.googleid.GoogleIdTokenCredential;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.SecureRandom;

/**
 * Native Google sign-in for engine/platform/google_auth_android.c.
 * Credential Manager shows the system account sheet (no browser); the Web
 * client id is the serverClientId, so the ID token's audience is the one
 * Supabase's Google provider expects. The token carries sha256(nonce); the
 * raw nonce goes back to C for Supabase to check.
 */
public final class GoogleAuth {
    private GoogleAuth() {}

    /** Delivered to C (handle = the GoogleAuth pointer); idToken null on failure. */
    static native void nativeResult(long handle, String idToken, String nonce, String error);

    public static void start(final Activity activity, final long handle, final String webClientId) {
        activity.runOnUiThread(() -> {
            try {
                final String rawNonce = randomNonce();
                GetGoogleIdOption option = new GetGoogleIdOption.Builder()
                        .setServerClientId(webClientId)
                        .setFilterByAuthorizedAccounts(false)
                        .setAutoSelectEnabled(false)
                        .setNonce(sha256Hex(rawNonce))
                        .build();
                GetCredentialRequest request = new GetCredentialRequest.Builder()
                        .addCredentialOption(option)
                        .build();
                CredentialManager.create(activity).getCredentialAsync(activity, request, new CancellationSignal(),
                        activity.getMainExecutor(),
                        new CredentialManagerCallback<GetCredentialResponse, GetCredentialException>() {
                            @Override
                            public void onResult(GetCredentialResponse response) {
                                Credential c = response.getCredential();
                                if (c instanceof CustomCredential
                                        && GoogleIdTokenCredential.TYPE_GOOGLE_ID_TOKEN_CREDENTIAL.equals(c.getType())) {
                                    GoogleIdTokenCredential g = GoogleIdTokenCredential.createFrom(c.getData());
                                    nativeResult(handle, g.getIdToken(), rawNonce, null);
                                } else {
                                    nativeResult(handle, null, null, "unexpected credential type " + c.getType());
                                }
                            }

                            @Override
                            public void onError(GetCredentialException e) {
                                String why = e instanceof GetCredentialCancellationException
                                        ? "cancelled" : e.getClass().getSimpleName() + ": " + e.getMessage();
                                nativeResult(handle, null, null, why);
                            }
                        });
            } catch (Exception e) {
                nativeResult(handle, null, null, e.toString());
            }
        });
    }

    private static String randomNonce() {
        byte[] b = new byte[24];
        new SecureRandom().nextBytes(b);
        StringBuilder sb = new StringBuilder();
        for (byte x : b) {
            sb.append(String.format("%02x", x));
        }
        return sb.toString();
    }

    private static String sha256Hex(String s) throws Exception {
        byte[] d = MessageDigest.getInstance("SHA-256").digest(s.getBytes(StandardCharsets.UTF_8));
        StringBuilder sb = new StringBuilder();
        for (byte x : d) {
            sb.append(String.format("%02x", x));
        }
        return sb.toString();
    }
}
