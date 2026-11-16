package com.caravan.monitor.ui

// Icons that match the touchscreen: Font Awesome 6 Free "droplet" for water
// and "faucet-drip" for the pumps (Font Awesome Free icons, CC BY 4.0,
// https://fontawesome.com/license/free). Kept as path data so no icon library
// is needed and the screenshot renderer draws them too.

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.graphics.vector.addPathNodes
import androidx.compose.ui.unit.dp

/** A square 512-unit icon; [dx] centres narrower glyphs. */
private fun fa(name: String, path: String, dx: Float = 0f): ImageVector =
    ImageVector.Builder(name, 24.dp, 24.dp, 512f, 512f).apply {
        addGroup(translationX = dx)
        addPath(addPathNodes(path), fill = SolidColor(Color.Black))
        clearGroup()
    }.build()

object CaravanIcons {
    /** FA6 droplet (tanks / water). */
    val Drop: ImageVector by lazy {
        fa("droplet", "M192 512C86 512 0 426 0 320C0 228.8 130.2 57.7 166.6 11.7C172.6 4.2 181.5 0 191.1 0h1.8c9.6 0 18.5 4.2 24.5 11.7C253.8 57.7 384 228.8 384 320c0 106-86 192-192 192zM96 336c0-8.8-7.2-16-16-16s-16 7.2-16 16c0 61.9 50.1 112 112 112c8.8 0 16-7.2 16-16s-7.2-16-16-16c-44.2 0-80-35.8-80-80z", dx = 64f)
    }

    /** FA6 faucet-drip (water pumps). */
    val Tap: ImageVector by lazy {
        fa("faucet-drip", "M224 0c17.7 0 32 14.3 32 32V44l96-12c17.7 0 32 14.3 32 32s-14.3 32-32 32L256 84l-31-3.9-1-.1-1 .1L192 84 96 96C78.3 96 64 81.7 64 64s14.3-32 32-32l96 12V32c0-17.7 14.3-32 32-32zM0 224c0-17.7 14.3-32 32-32h96l22.6-22.6c6-6 14.1-9.4 22.6-9.4H192V116.2l32-4 32 4V160h18.7c8.5 0 16.6 3.4 22.6 9.4L320 192h32c88.4 0 160 71.6 160 160c0 17.7-14.3 32-32 32H416c-17.7 0-32-14.3-32-32s-14.3-32-32-32H315.9c-20.2 29-53.9 48-91.9 48s-71.7-19-91.9-48H32c-17.7 0-32-14.3-32-32V224zM436.8 423.4c1.9-4.5 6.3-7.4 11.2-7.4s9.2 2.9 11.2 7.4l18.2 42.4c1.8 4.1 2.7 8.6 2.7 13.1V480c0 17.7-14.3 32-32 32s-32-14.3-32-32v-1.2c0-4.5 .9-8.9 2.7-13.1l18.2-42.4z")
    }
}
