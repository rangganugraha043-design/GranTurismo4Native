package id.granturismo4.nativeapp

import android.app.Activity
import android.os.Bundle
import android.content.Intent
import android.net.Uri
import android.view.Gravity
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView

class MainActivity : Activity() {
    private external fun nativeVersion(): String
    private external fun inspectIsoHeader(fd: Int): String
    private lateinit var status: TextView
    private val pickIso = 41

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        System.loadLibrary("gt4native")
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL; gravity = Gravity.CENTER
            setPadding(28, 36, 28, 36)
        }
        val title = TextView(this).apply { text = "GRAN TURISMO 4 NATIVE"; textSize = 24f; gravity = Gravity.CENTER }
        val subtitle = TextView(this).apply { text = "Android ARM64 development build"; textSize = 14f; gravity = Gravity.CENTER }
        status = TextView(this).apply { text = "Pilih image game milikmu untuk pemeriksaan format."; textSize = 15f; setPadding(0,32,0,24) }
        val choose = Button(this).apply { text = "PILIH FILE ISO"; setOnClickListener { openIsoPicker() } }
        val controls = Button(this).apply { text = "CONTROLLER: TOUCH + BLUETOOTH"; isEnabled = false }
        val build = TextView(this).apply { text = "Native core: " + nativeVersion() + "\nStatus: ISO inspection prototype — belum menjalankan game."; textSize = 12f; setPadding(0,24,0,0) }
        root.addView(title); root.addView(subtitle); root.addView(status); root.addView(choose); root.addView(controls); root.addView(build)
        setContentView(root)
    }

    private fun openIsoPicker() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE); type = "*/*"
        }
        startActivityForResult(intent, pickIso)
    }

    @Deprecated("Legacy result callback used for broad Android compatibility")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == pickIso && resultCode == RESULT_OK) {
            val uri: Uri = data?.data ?: return
            try {
                contentResolver.openFileDescriptor(uri, "r")?.use { pfd ->
                    status.text = "File dipilih: " + (uri.lastPathSegment ?: "ISO") + "\n" + inspectIsoHeader(pfd.fd)
                }
            } catch (e: Exception) { status.text = "Gagal membaca file: " + e.message }
        }
    }
    companion object { init { System.loadLibrary("gt4native") } }
}
