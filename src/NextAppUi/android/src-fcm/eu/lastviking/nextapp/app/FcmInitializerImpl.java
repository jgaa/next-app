package eu.lastviking.nextapp.app;

import android.Manifest;
import android.content.pm.PackageManager;
import android.os.Build;
import android.util.Log;

import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import com.google.firebase.FirebaseApp;
import com.google.firebase.FirebaseOptions;
import com.google.firebase.messaging.FirebaseMessaging;

public final class FcmInitializerImpl {
    private static final String TAG = "FcmInitializerImpl";
    private static final int REQUEST_CODE_POST_NOTIFICATIONS = 1001;

    private FcmInitializerImpl() {
    }

    public static void initialize(MainActivity activity) {
        ensureFirebaseInitializedAndLogProjectInfo(activity);

        FirebaseMessaging.getInstance()
            .getToken()
            .addOnCompleteListener(task -> {
                if (task.isSuccessful()) {
                    MainActivity.setFcmToken(task.getResult());
                    Log.d(TAG, "Initial token: " + MainActivity.getFcmToken());
                } else {
                    Log.e(TAG, "Token fetch failed", task.getException());
                }
            });

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (ContextCompat.checkSelfPermission(activity, Manifest.permission.POST_NOTIFICATIONS)
                    != PackageManager.PERMISSION_GRANTED) {
                ActivityCompat.requestPermissions(
                    activity,
                    new String[]{Manifest.permission.POST_NOTIFICATIONS},
                    REQUEST_CODE_POST_NOTIFICATIONS
                );
            }
        }
    }

    private static void ensureFirebaseInitializedAndLogProjectInfo(MainActivity activity) {
        FirebaseApp app;
        try {
            app = FirebaseApp.getInstance();
        } catch (IllegalStateException e) {
            app = FirebaseApp.initializeApp(activity);
        }

        if (app == null) {
            Log.e(TAG, "FirebaseApp is null. Is google-services.json included for this package?");
            return;
        }

        FirebaseOptions options = app.getOptions();
        MainActivity.setFcmProjectId(options.getProjectId());
        String senderId = options.getGcmSenderId();
        String appId = options.getApplicationId();
        String apiKey = options.getApiKey();

        Log.i(TAG, "Firebase options: "
                + "projectId=" + MainActivity.getFcmProjectId()
                + ", senderId=" + senderId
                + ", appId=" + appId
                + ", apiKeyPresent=" + (apiKey != null && !apiKey.isEmpty()));

        logFirebaseResourceHints(activity);
    }

    private static void logFirebaseResourceHints(MainActivity activity) {
        try {
            String pkg = activity.getPackageName();
            int idSender = activity.getResources().getIdentifier("gcm_defaultSenderId", "string", pkg);
            int idProj = activity.getResources().getIdentifier("project_id", "string", pkg);
            int idAppId = activity.getResources().getIdentifier("default_web_client_id", "string", pkg);

            String resourceSender = (idSender != 0) ? activity.getString(idSender) : "<missing>";
            String resourceProj = (idProj != 0) ? activity.getString(idProj) : "<missing>";
            String resourceAppId = (idAppId != 0) ? activity.getString(idAppId) : "<missing>";

            Log.i(TAG, "Firebase resources: gcm_defaultSenderId=" + resourceSender
                    + ", project_id=" + resourceProj
                    + ", default_web_client_id=" + resourceAppId);
        } catch (Throwable t) {
            Log.w(TAG, "Could not read generated Firebase resources", t);
        }
    }
}
