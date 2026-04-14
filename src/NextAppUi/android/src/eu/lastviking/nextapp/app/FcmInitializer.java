package eu.lastviking.nextapp.app;

import android.util.Log;

import java.lang.reflect.Method;

public final class FcmInitializer {
    private static final String TAG = "FcmInitializer";
    private static final String IMPL_CLASS = "eu.lastviking.nextapp.app.FcmInitializerImpl";

    private FcmInitializer() {
    }

    public static void initialize(MainActivity activity) {
        try {
            Class<?> cls = Class.forName(IMPL_CLASS);
            Method method = cls.getMethod("initialize", MainActivity.class);
            method.invoke(null, activity);
        } catch (ClassNotFoundException e) {
            Log.d(TAG, "FCM support not compiled in; skipping initialization");
        } catch (Exception e) {
            Log.e(TAG, "Failed to initialize optional FCM support", e);
        }
    }
}
