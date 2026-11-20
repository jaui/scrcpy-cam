// AGP 9 compiles Kotlin itself; no separate Kotlin plugin needed.
import java.util.Properties

plugins {
    alias(libs.plugins.android.application)
}

val APP_VERSION_CODE = 2
val APP_VERSION_NAME = "1.1.0"

/**
 * Release signing material, kept out of the repository. Looked up in this order:
 *  1. environment variable ADBWIFI_KEYSTORE_PROPERTIES (path to a .properties file)
 *  2. keystore.properties next to settings.gradle.kts (gitignored)
 * Keys: storeFile, storePassword, keyAlias, keyPassword. Without it the release build stays
 * unsigned; the debug build always works.
 */
val keystorePropertiesFile = System.getenv("ADBWIFI_KEYSTORE_PROPERTIES")
    ?.let { file(it) }
    ?: rootProject.file("keystore.properties")
val keystoreProperties = Properties().apply {
    if (keystorePropertiesFile.exists()) {
        keystorePropertiesFile.inputStream().use { load(it) }
    }
}

android {
    namespace = "com.github.jaui.adbwifi"
    compileSdk = libs.versions.compileSdk.get().toInt()

    defaultConfig {
        applicationId = "com.github.jaui.adbwifi"
        minSdk = libs.versions.minSdk.get().toInt()
        targetSdk = libs.versions.targetSdk.get().toInt()
        versionCode = APP_VERSION_CODE
        versionName = APP_VERSION_NAME
    }

    signingConfigs {
        if (keystorePropertiesFile.exists()) {
            create("release") {
                storeFile = keystorePropertiesFile.parentFile.resolve(
                    keystoreProperties.getProperty("storeFile"))
                storePassword = keystoreProperties.getProperty("storePassword")
                keyAlias = keystoreProperties.getProperty("keyAlias")
                keyPassword = keystoreProperties.getProperty("keyPassword")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"))
            signingConfigs.findByName("release")?.let { signingConfig = it }
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        buildConfig = true
    }

    lint {
        // WRITE_SECURE_SETTINGS is granted via adb on purpose; this app is not for the Play Store.
        disable += "ProtectedPermissions"
        // "Newer version available" checks change daily and say nothing about this code.
        disable += listOf("AndroidGradlePluginVersion", "GradleDependency")
        warningsAsErrors = true
    }
}

dependencies {
    testImplementation(libs.junit)
}
