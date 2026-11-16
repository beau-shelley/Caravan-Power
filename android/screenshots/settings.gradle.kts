// Renders the app's screens to PNGs on a desktop JVM (no Android SDK or emulator).
// Uses the GCS mirror of Maven Central because repo.maven.apache.org rate-limits
// some CI/cloud networks; falls back to Maven Central.
pluginManagement {
    repositories {
        maven("https://maven-central.storage-download.googleapis.com/maven2/")
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositories {
        maven("https://maven-central.storage-download.googleapis.com/maven2/")
        mavenCentral()
    }
}
rootProject.name = "caravan-screenshots"
