package jp.snesdq5.player;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;
import android.content.Context;
import android.view.SurfaceHolder;
import android.view.WindowInsets;
import android.os.Build;
import android.graphics.Insets;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.widget.Toast;
import java.io.*;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;

/** Android Storage Access Framework adapter. No broad storage permission is used. */
public class MainActivity extends SDLActivity {
    private static final int STORAGE = 41, ROM = 42;
    private boolean acceptingSurface;
    private volatile int[] safeInsets=new int[]{0,0,0,0};
    public int[] getSafeInsets() { return safeInsets.clone(); }
    private void updateSafeInsets(WindowInsets insets) {
        int left,top,right,bottom;
        if(Build.VERSION.SDK_INT>=30) {
            Insets bars=insets.getInsetsIgnoringVisibility(WindowInsets.Type.systemBars()|WindowInsets.Type.displayCutout());
            left=bars.left;top=bars.top;right=bars.right;bottom=bars.bottom;
        } else {
            left=insets.getStableInsetLeft();top=insets.getStableInsetTop();right=insets.getStableInsetRight();bottom=insets.getStableInsetBottom();
            if(Build.VERSION.SDK_INT>=28 && insets.getDisplayCutout()!=null) {
                android.view.DisplayCutout cutout=insets.getDisplayCutout();
                left=Math.max(left,cutout.getSafeInsetLeft());top=Math.max(top,cutout.getSafeInsetTop());
                right=Math.max(right,cutout.getSafeInsetRight());bottom=Math.max(bottom,cutout.getSafeInsetBottom());
            }
        }
        int[] origin=new int[2]; mSurface.getLocationInWindow(origin);
        android.view.View decor=getWindow().getDecorView();
        safeInsets=new int[]{Math.max(0,left-origin[0]),Math.max(0,top-origin[1]),
            Math.max(0,origin[0]+mSurface.getWidth()-decor.getWidth()+right),
            Math.max(0,origin[1]+mSurface.getHeight()-decor.getHeight()+bottom)};
    }
    @Override protected SDLSurface createSDLSurface(Context context) {
        return new SDLSurface(context) {
            @Override public void surfaceChanged(SurfaceHolder holder,int format,int width,int height) {
                // This UI supports both aspect ratios. A system rotation override must never block SDL startup.
                acceptingSurface=true;
                try { super.surfaceChanged(holder,format,width,height); }
                finally { acceptingSurface=false; }
                WindowInsets insets=getRootWindowInsets(); if(insets!=null) updateSafeInsets(insets);
            }
        };
    }
    @Override public int getRequestedOrientation() {
        return acceptingSurface?ActivityInfo.SCREEN_ORIENTATION_FULL_SENSOR:super.getRequestedOrientation();
    }

    private volatile String pickedRom = "";
    private volatile String storageError = "";
    @Override protected String[] getLibraries() { return new String[] {"main"}; }
    private SharedPreferences preferences() { return getSharedPreferences("storage", MODE_PRIVATE); }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        mSurface.setOnApplyWindowInsetsListener((view,insets)->{ updateSafeInsets(insets); return insets; });
        mSurface.requestApplyInsets();
        String saved = preferences().getString("rom", "");
        if (!saved.isEmpty()) {
            try { pickedRom = cacheRom(Uri.parse(saved)); }
            catch (Exception e) { storageError = "前回のROMを開けません。再選択してください。"; }
        }
    }
    // Let Android's sensors select the orientation. Do not lock to SDL's initial window aspect ratio.
    @Override public void setOrientationBis(int width,int height,boolean resizable,String hint) {}
    public String consumePickedRom() {
        String result = pickedRom; pickedRom = ""; return result;
    }
    public void chooseStorage() { runOnUiThread(() -> {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI, Uri.parse("content://com.android.externalstorage.documents/document/primary%3ADocuments"));
        startActivityForResult(intent, STORAGE);
    }); }
    public void chooseRom() { runOnUiThread(() -> {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.setType("*/*"); intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try {
            Uri root = root();
            if (root != null) intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI, child(root, "rom", DocumentsContract.Document.MIME_TYPE_DIR, true));
        } catch (Exception e) { storageError = e.toString(); }
        startActivityForResult(intent, ROM);
    }); }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (result != Activity.RESULT_OK || data == null || data.getData() == null) return;
        try {
            Uri uri = data.getData();
            int flags = data.getFlags() & (Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
            getContentResolver().takePersistableUriPermission(uri, flags);
            if (request == STORAGE) {
                Uri tree = DocumentsContract.buildDocumentUriUsingTree(uri, DocumentsContract.getTreeDocumentId(uri));
                Uri folder = name(tree).equals("SnesDQ5") ? tree : child(tree, "SnesDQ5", DocumentsContract.Document.MIME_TYPE_DIR, true);
                child(folder, "rom", DocumentsContract.Document.MIME_TYPE_DIR, true);
                // Migrate local files only after the selected location is writable.
                for (File file : getFilesDir().listFiles()) {
                    if (file.isFile() && (file.getName().endsWith(".srm") || file.getName().endsWith(".srm.meta") || file.getName().endsWith(".state") || file.getName().equals("settings.ini"))) {
                        Uri existing = child(folder, file.getName(), "application/octet-stream", false);
                        if (existing == null) writeDocument(folder, file.getName(), Files.readAllBytes(file.toPath()));
                    }
                }
                preferences().edit().putString("tree", folder.toString()).apply();
                Toast.makeText(this, "SnesDQ5/rom を準備しました", Toast.LENGTH_LONG).show();
            } else if (request == ROM) {
                String path = cacheRom(uri);
                preferences().edit().putString("rom", uri.toString()).apply();
                pickedRom = path;
            }
        } catch (Exception e) { storageError = e.toString(); Toast.makeText(this, "保存先またはROMを開けません: " + e.getMessage(), Toast.LENGTH_LONG).show(); }
    }
    private Uri root() {
        String tree = preferences().getString("tree", ""); return tree.isEmpty() ? null : Uri.parse(tree);
    }
    private String name(Uri uri) throws IOException {
        try (Cursor cursor = getContentResolver().query(uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor == null || !cursor.moveToFirst()) throw new IOException("Document name unavailable");
            return cursor.getString(0);
        }
    }
    private Uri child(Uri parent, String name, String mime, boolean create) throws IOException {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(parent, DocumentsContract.getDocumentId(parent));
        try (Cursor cursor = getContentResolver().query(children, new String[] {DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME}, null, null, null)) {
            if (cursor == null) throw new IOException("Folder cannot be read");
            while (cursor.moveToNext()) if (name.equals(cursor.getString(1))) return DocumentsContract.buildDocumentUriUsingTree(parent, cursor.getString(0));
        }
        if (!create) return null;
        Uri result = DocumentsContract.createDocument(getContentResolver(), parent, mime, name);
        if (result == null) throw new IOException("Could not create " + name); return result;
    }
    private String safeName(String name) throws IOException {
        if (name.isEmpty() || name.contains("/") || name.contains("\\") || name.equals(".") || name.equals("..")) throw new IOException("Invalid file name");
        return name;
    }
    private String cacheRom(Uri uri) throws IOException {
        String filename = safeName(name(uri));
        if (!(filename.toLowerCase(java.util.Locale.ROOT).endsWith(".smc") || filename.toLowerCase(java.util.Locale.ROOT).endsWith(".sfc"))) throw new IOException("Select an .smc or .sfc ROM");
        File directory = new File(getFilesDir(), "rom"); directory.mkdirs();
        File destination = new File(directory, filename), temporary = new File(directory, filename + ".tmp");
        try (InputStream in = getContentResolver().openInputStream(uri); OutputStream out = new FileOutputStream(temporary)) {
            if (in == null) throw new IOException("ROM unavailable");
            byte[] buffer = new byte[65536]; int count, total = 0;
            while ((count = in.read(buffer)) != -1) {
                total += count; if (total > 0x1000200) throw new IOException("ROM too large"); out.write(buffer, 0, count);
            }
        }
        Files.move(temporary.toPath(), destination.toPath(), StandardCopyOption.REPLACE_EXISTING);
        return destination.getAbsolutePath();
    }
    public synchronized byte[] readData(String filename) throws IOException {
        safeName(filename); Uri root = root();
        if (root == null) { File file = new File(getFilesDir(), filename); return file.exists() ? Files.readAllBytes(file.toPath()) : null; }
        Uri file = child(root, filename, "application/octet-stream", false);
        if (file == null) return null;
        try (InputStream in = getContentResolver().openInputStream(file); ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            if (in == null) throw new IOException("Could not read save");
            byte[] buffer = new byte[65536]; int count;
            while ((count = in.read(buffer)) != -1) out.write(buffer, 0, count);
            return out.toByteArray();
        }
    }
    private void writeDocument(Uri root, String filename, byte[] bytes) throws IOException {
        Uri file = child(root, filename, "application/octet-stream", true);
        try (OutputStream out = getContentResolver().openOutputStream(file, "wt")) {
            if (out == null) throw new IOException("Could not write save"); out.write(bytes);
        }
    }
    public synchronized void writeData(String filename, byte[] bytes) throws IOException {
        safeName(filename);
        // Always keep a complete internal copy; do not silently hide a Documents write failure.
        File dest = new File(getFilesDir(), filename), temporary = new File(getFilesDir(), filename + ".tmp");
        Files.write(temporary.toPath(), bytes); Files.move(temporary.toPath(), dest.toPath(), StandardCopyOption.REPLACE_EXISTING);
        Uri root = root(); if (root != null) {
            byte[] previous = readData(filename);
            if (previous != null) writeDocument(root, filename + ".bak", previous);
            writeDocument(root, filename, bytes);
        }
    }
}
