package com.shahriyar.smartwificharger.presentation.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.shahriyar.smartwificharger.domain.model.ConnectionMode
import com.shahriyar.smartwificharger.presentation.theme.AmberAlert
import com.shahriyar.smartwificharger.presentation.theme.CoralRed
import com.shahriyar.smartwificharger.presentation.theme.ElectricGreen
import com.shahriyar.smartwificharger.presentation.theme.NeonCyan

@Composable
fun ConnectionBadge(
    mode: ConnectionMode,
    isMcuOnline: Boolean = true,
    modifier: Modifier = Modifier
) {
    val (label, dotColor, bgColor) = when {
        !isMcuOnline || mode == ConnectionMode.OFFLINE || mode == ConnectionMode.ERROR -> {
            Triple("Offline", CoralRed, Color(0x26FF5252))
        }
        mode == ConnectionMode.CONNECTING -> {
            Triple("Connecting...", Color(0xFF94A3B8), Color(0x2694A3B8))
        }
        mode == ConnectionMode.DIRECT_AP -> {
            Triple("Direct (AP)", AmberAlert, Color(0x26FFB300))
        }
        mode == ConnectionMode.LOCAL_LAN -> {
            Triple("Online", ElectricGreen, Color(0x2600E676))
        }
        mode == ConnectionMode.FIREBASE_WAN -> {
            Triple("Online (Cloud)", NeonCyan, Color(0x2600E5FF))
        }
        else -> {
            Triple("Offline", CoralRed, Color(0x26FF5252))
        }
    }

    Box(
        modifier = modifier
            .clip(RoundedCornerShape(16.dp))
            .background(bgColor)
            .border(1.dp, dotColor.copy(alpha = 0.5f), RoundedCornerShape(16.dp))
            .padding(horizontal = 10.dp, vertical = 5.dp)
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Box(
                modifier = Modifier
                    .size(8.dp)
                    .clip(CircleShape)
                    .background(dotColor)
            )
            Spacer(modifier = Modifier.width(6.dp))
            Text(
                text = label,
                fontSize = 11.sp,
                fontWeight = FontWeight.Bold,
                color = dotColor,
                letterSpacing = 0.5.sp
            )
        }
    }
}
