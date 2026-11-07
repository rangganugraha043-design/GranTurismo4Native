package id.granturismo4.nativeapp

import android.app.Activity
import android.os.Bundle
import android.content.Intent
import android.net.Uri
import android.provider.OpenableColumns
import android.view.Gravity
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView

class MainActivity : Activity() {
    private external fun nativeVersion(): String
    private external fun inspectIsoHeader(fd: Int): String
    private lateinit var status: TextView
    private val pickIso = 41
    private val preferences by lazy { getSharedPreferences("gt4_native", MODE_PRIVATE) }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
            setPadding(28, 36, 28, 36)
        }
        val title = TextView(this).apply {
            text = "GRAN TURISMO 4 NATIVE"
            textSize = 24f
            gravity = Gravity.CENTER
        }
        val subtitle = TextView(this).apply {
            text = "Android ARM64 development build"
            textSize = 14f
            gravity = Gravity.CENTER
        }
        status = TextView(this).apply {
            text = "Pilih image game milikmu untuk pemeriksaan format."
            textSize = 15f
            setPadding(0, 32, 0, 24)
        }
        val choose = Button(this).apply {
            text = "PILIH FILE ISO"
            setOnClickListener { openIsoPicker() }
        }
        val build = TextView(this).apply {
            text = "Native core: " + nativeVersion() +
                "\nStatus: ISO inspection prototype — belum menjalankan game."
            textSize = 12f
            setPadding(0, 24, 0, 0)
        }
        root.addView(title)
        root.addView(subtitle)
        root.addView(status)
        root.addView(choose)
        root.addView(build)
        setContentView(root)

        // Reuse the user's previously selected disc when Android still grants access.
        preferences.getString("last_disc_uri", null)?.let { saved ->
            try {
                inspectSelectedDisc(Uri.parse(saved))
            } catch (_: Exception) {
                status.text = "Akses ISO sebelumnya tidak tersedia. Silakan pilih ulang file ISO."
            }
        }
    }

    private fun openIsoPicker() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "*/*"
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or
                Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION)
        }
        startActivityForResult(intent, pickIso)
    }

    private fun displayName(uri: Uri): String {
        if (uri.scheme == "content") {
            contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor ->
                if (cursor.moveToFirst()) {
                    val index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                    if (index >= 0) return cursor.getString(index) ?: "Disc image"
                }
            }
        }
        return uri.lastPathSegment ?: "Disc image"
    }

    private fun inspectSelectedDisc(uri: Uri) {
        contentResolver.openFileDescriptor(uri, "r")?.use { pfd ->
            status.text = "File dipilih: " + displayName(uri) + "\n" + inspectIsoHeader(pfd.fd)
        } ?: run {
            status.text = "File tidak dapat dibuka. Pilih file ISO lain."
        }
    }

    @Deprecated("Legacy result callback used for broad Android compatibility")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != pickIso || resultCode != RESULT_OK) return
        val uri = data?.data ?: return
        val grantedFlags = (data.flags and Intent.FLAG_GRANT_READ_URI_PERMISSION)
        try {
            if (grantedFlags != 0) {
                contentResolver.takePersistableUriPermission(uri, grantedFlags)
                preferences.edit().putString("last_disc_uri", uri.toString()).apply()
            }
        } catch (_: SecurityException) {
            // Some document providers offer temporary access only; inspect this selection anyway.
        }
        try {
            inspectSelectedDisc(uri)
        } catch (e: Exception) {
            status.text = "Gagal membaca file: " + (e.message ?: "kesalahan tidak diketahui")
        }
    }

    companion object {
        init { System.loadLibrary("gt4native") }
    }
}
