// The game's activity: SDL's activity with the game in libmain.so (its main function), the gameplay configs copied from
// the APK's assets into the app's storage before the game starts, and the game folder picker that the game's thread
// asks for (showDirectorySelection) and waits on until gotDirectory. After Julius' JuliusMainActivity
// (https://github.com/bvschaik/julius, AGPL-3.0).

package com.github.dalerank.tutankhamun;

import android.content.Intent;
import android.content.res.AssetManager;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;

public class TutankhamunActivity extends SDLActivity {
    private static final String TAG = "tutankhamun";
    private static final String CONFIG_ASSETS = "config";
    private static final int GET_FOLDER_RESULT = 500;
    private static final int HTTP_TIMEOUT_MS = 15000;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        copyConfigs();
        super.onCreate(savedInstanceState);
    }

    @Override
    public void onStop() {
        super.onStop();
        FileManager.clearCache();
    }

    @Override
    protected String[] getLibraries() {
        return new String[]{"SDL2", "SDL2_mixer", "main"};
    }

    @Override
    protected String getMainFunction() {
        return "main";
    }

    private void copyConfigs() {
        AssetManager assets = getAssets();
        File target = new File(getFilesDir(), CONFIG_ASSETS);
        if (!target.isDirectory() && !target.mkdirs()) {
            Log.e(TAG, "cannot make " + target);
            return;
        }
        byte[] buffer = new byte[65536];
        try {
            String[] names = assets.list(CONFIG_ASSETS);
            if (names == null) {
                return;
            }
            for (String name : names) {
                try (InputStream in = assets.open(CONFIG_ASSETS + "/" + name);
                     OutputStream out = new FileOutputStream(new File(target, name))) {
                    int read;
                    while ((read = in.read(buffer)) > 0) {
                        out.write(buffer, 0, read);
                    }
                }
            }
        } catch (IOException e) {
            Log.e(TAG, "cannot copy the configs: " + e);
        }
    }

    @SuppressWarnings("unused")
    public void showDirectorySelection(boolean again) {
        startActivityForResult(DirectorySelectionActivity.newIntent(this, again), GET_FOLDER_RESULT);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == GET_FOLDER_RESULT) {
            if (resultCode == RESULT_OK && data != null && data.getData() != null) {
                FileManager.setBaseUri(data.getData());
            } else {
                FileManager.setBaseUri(Uri.EMPTY);
            }
            gotDirectory();
        } else {
            super.onActivityResult(requestCode, resultCode, data);
        }
    }

    // a bug report's POST, from the game's report thread; the answer's status, 0 when there was none
    @SuppressWarnings("unused")
    public static int httpPostJson(String url, byte[] body) {
        HttpURLConnection connection = null;
        try {
            connection = (HttpURLConnection) new URL(url).openConnection();
            connection.setConnectTimeout(HTTP_TIMEOUT_MS);
            connection.setReadTimeout(HTTP_TIMEOUT_MS * 4);
            connection.setRequestMethod("POST");
            connection.setDoOutput(true);
            connection.setRequestProperty("Content-Type", "application/json");
            connection.setRequestProperty("User-Agent", "tutankhamun");
            connection.setFixedLengthStreamingMode(body.length);
            try (OutputStream out = connection.getOutputStream()) {
                out.write(body);
            }
            int status = connection.getResponseCode();
            try (InputStream in = status < 400 ? connection.getInputStream() : connection.getErrorStream()) {
                if (in != null) {
                    byte[] buffer = new byte[4096];
                    while (in.read(buffer) > 0) {
                        // the answer is not needed: read for the connection's reuse
                    }
                }
            }
            return status;
        } catch (IOException e) {
            Log.e(TAG, "the POST to " + url + " failed: " + e);
            return 0;
        } finally {
            if (connection != null) {
                connection.disconnect();
            }
        }
    }

    private native void gotDirectory();
}
