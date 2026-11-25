// Compose Multiplatform 1.5 is the newest desktop Compose that needs nothing
// from Google's Maven repo, which some build networks can't reach.
plugins {
    kotlin("jvm") version "1.9.24"
    id("org.jetbrains.compose") version "1.5.12"
    application
}

compose { kotlinCompilerPlugin.set("1.5.14") }

kotlin { jvmToolchain(21) }

// The app's UI code that doesn't touch Android: the model and the *Content screens.
val app = "../app/src/main/java/com/caravan/monitor"
sourceSets.main {
    kotlin.srcDir("src/main/kotlin")
    kotlin.srcDir(layout.buildDirectory.dir("shared"))
}
val copyShared by tasks.registering(Copy::class) {
    from(app) {
        include("Model.kt", "Usage.kt")
        include("ui/Colors.kt", "ui/Common.kt", "ui/HomeContent.kt", "ui/AlertsContent.kt",
            "ui/RuleEditorContent.kt", "ui/ClimateContent.kt", "ui/SetupContent.kt", "ui/Icons.kt", "ui/UpdateContent.kt", "ui/AppScaffold.kt",
            "ui/CamoPaths.kt", "ui/Texture.kt", "ui/Fit.kt", "ui/AdminContent.kt", "ui/LevelContent.kt")
    }
    into(layout.buildDirectory.dir("shared"))
}
tasks.named("compileKotlin") { dependsOn(copyShared) }

dependencies {
    implementation(compose.desktop.currentOs)
    implementation(compose.material3)
    implementation(compose.materialIconsExtended)
    implementation("org.json:json:20240303")
}

application { mainClass.set("ShotsKt") }
tasks.named<JavaExec>("run") {
    workingDir = projectDir
    systemProperty("java.awt.headless", "true")
}
