package com.mccluster.parkour;

import org.bukkit.Bukkit;
import org.bukkit.ChatColor;
import org.bukkit.Location;
import org.bukkit.Material;
import org.bukkit.command.Command;
import org.bukkit.command.CommandExecutor;
import org.bukkit.command.CommandSender;
import org.bukkit.entity.Player;
import org.bukkit.event.EventHandler;
import org.bukkit.event.Listener;
import org.bukkit.event.block.Action;
import org.bukkit.event.player.PlayerInteractEvent;
import org.bukkit.event.player.PlayerJoinEvent;
import org.bukkit.event.player.PlayerQuitEvent;
import org.bukkit.plugin.java.JavaPlugin;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;

/**
 * ParkourVault 极简跑酷同步插件
 * 负责监听玩家冲线/进退服，向分布式数据代理微服务 (ender-vault) 发起异步请求
 */
public class ParkourPlugin extends JavaPlugin implements Listener, CommandExecutor {

    // EnderVault 微服务内网地址 (默认通过 Docker 服务名寻址)
    private String enderVaultUrl = "http://ender-vault:8080";

    // 记录玩家跑酷起跑时间戳 (UUID -> StartTimeMillis)
    private final Map<UUID, Long> startTimes = new ConcurrentHashMap<>();

    @Override
    public void onEnable() {
        saveDefaultConfig();
        enderVaultUrl = getConfig().getString("ender_vault_url", "http://ender-vault:8080");

        getServer().getPluginManager().registerEvents(this, this);
        if (getCommand("parkour") != null) {
            getCommand("parkour").setExecutor(this);
        }
        getLogger().info("ParkourVault plugin loaded, connected to EnderVault at: " + enderVaultUrl);
    }

    @Override
    public void onDisable() {
        startTimes.clear();
        getLogger().info("ParkourVault plugin disabled.");
    }

    // -------------------------------------------------------------------------
    // 玩家进退服事件
    // -------------------------------------------------------------------------

    @EventHandler
    public void onPlayerJoin(PlayerJoinEvent event) {
        Player player = event.getPlayer();
        String uuid = player.getUniqueId().toString();
        String name = player.getName();

        // 异步向 ender-vault 查询/注册玩家数据
        Bukkit.getScheduler().runTaskAsynchronously(this, () -> {
            try {
                String endpoint = enderVaultUrl + "/api/v1/player/" + uuid + "/data?username=" + name;
                HttpURLConnection conn = (HttpURLConnection) new URL(endpoint).openConnection();
                conn.setRequestMethod("GET");
                conn.setConnectTimeout(3000);
                conn.setReadTimeout(3000);

                if (conn.getResponseCode() == 200) {
                    player.sendMessage(ChatColor.GOLD + "[EnderVault] " + ChatColor.GREEN + "玩家资产跨服数据同步成功！");
                }
                conn.disconnect();
            } catch (Exception e) {
                getLogger().warning("Failed to fetch player vault data for " + name + ": " + e.getMessage());
            }
        });
    }

    @EventHandler
    public void onPlayerQuit(PlayerQuitEvent event) {
        UUID uuid = event.getPlayer().getUniqueId();
        startTimes.remove(uuid);

        // 异步上报更新最后服务器为 paper-parkour
        Bukkit.getScheduler().runTaskAsynchronously(this, () -> {
            try {
                String endpoint = enderVaultUrl + "/api/v1/player/" + uuid.toString() + "/data";
                HttpURLConnection conn = (HttpURLConnection) new URL(endpoint).openConnection();
                conn.setRequestMethod("POST");
                conn.setRequestProperty("Content-Type", "application/json");
                conn.setDoOutput(true);
                conn.setConnectTimeout(3000);

                String payload = "{\"last_server\":\"paper-parkour\"}";
                try (OutputStream os = conn.getOutputStream()) {
                    os.write(payload.getBytes(StandardCharsets.UTF_8));
                }
                conn.getResponseCode();
                conn.disconnect();
            } catch (Exception ignored) {}
        });
    }

    // -------------------------------------------------------------------------
    // 跑酷起跑与冲线逻辑 (踩压力板触发)
    // -------------------------------------------------------------------------

    @EventHandler
    public void onPlayerInteract(PlayerInteractEvent event) {
        if (event.getAction() != Action.PHYSICAL || event.getClickedBlock() == null) {
            return;
        }

        Material mat = event.getClickedBlock().getType();
        Player player = event.getPlayer();
        UUID uuid = player.getUniqueId();

        // 铁质压力板 (HEAVY_WEIGHTED_PRESSURE_PLATE / IRON_PLATE) -> 起点
        if (mat.name().contains("IRON") || mat.name().contains("HEAVY")) {
            startTimes.put(uuid, System.currentTimeMillis());
            player.sendMessage(ChatColor.AQUA + "⏱ 跑酷计时开始！冲向终点！");
            return;
        }

        // 金质压力板 (LIGHT_WEIGHTED_PRESSURE_PLATE / GOLD_PLATE) -> 终点冲线
        if (mat.name().contains("GOLD") || mat.name().contains("LIGHT")) {
            Long startTime = startTimes.remove(uuid);
            if (startTime == null) {
                player.sendMessage(ChatColor.RED + "你尚未踩过起点压力板！");
                return;
            }

            long elapsedMs = System.currentTimeMillis() - startTime;
            double seconds = elapsedMs / 1000.0;
            player.sendMessage(ChatColor.GREEN + "🎉 恭喜冲线！本次耗时: " + String.format("%.2f", seconds) + " 秒");

            // 异步上报成绩给 ender-vault 微服务
            reportParkourFinish(player, elapsedMs);
        }
    }

    private void reportParkourFinish(Player player, long timeMs) {
        Bukkit.getScheduler().runTaskAsynchronously(this, () -> {
            try {
                String endpoint = enderVaultUrl + "/api/v1/parkour/finish";
                HttpURLConnection conn = (HttpURLConnection) new URL(endpoint).openConnection();
                conn.setRequestMethod("POST");
                conn.setRequestProperty("Content-Type", "application/json");
                conn.setDoOutput(true);
                conn.setConnectTimeout(3000);

                String payload = String.format(
                        "{\"player_uuid\":\"%s\",\"player_name\":\"%s\",\"track_id\":\"rooftop_sprint\",\"time_ms\":%d,\"checkpoints\":1}",
                        player.getUniqueId().toString(), player.getName(), timeMs
                );

                try (OutputStream os = conn.getOutputStream()) {
                    os.write(payload.getBytes(StandardCharsets.UTF_8));
                }

                int respCode = conn.getResponseCode();
                if (respCode == 200) {
                    Bukkit.broadcastMessage(ChatColor.GOLD + "[跑酷全服通报] " +
                            ChatColor.YELLOW + player.getName() +
                            ChatColor.WHITE + " 成功通关屋顶跑酷！用时 " +
                            ChatColor.AQUA + String.format("%.2f", timeMs / 1000.0) + "s" +
                            ChatColor.GREEN + " 获得 100 硬币奖励！");
                }
                conn.disconnect();
            } catch (Exception e) {
                getLogger().warning("Failed to report finish for " + player.getName() + ": " + e.getMessage());
            }
        });
    }

    // -------------------------------------------------------------------------
    // 指令系统 (/parkour top)
    // -------------------------------------------------------------------------

    @Override
    public boolean onCommand(CommandSender sender, Command command, String label, String[] args) {
        if (args.length > 0 && args[0].equalsIgnoreCase("top")) {
            sender.sendMessage(ChatColor.YELLOW + "=== 正在拉取跑酷排行榜... ===");
            Bukkit.getScheduler().runTaskAsynchronously(this, () -> {
                try {
                    String endpoint = enderVaultUrl + "/api/v1/parkour/leaderboard?track_id=rooftop_sprint&limit=5";
                    HttpURLConnection conn = (HttpURLConnection) new URL(endpoint).openConnection();
                    conn.setRequestMethod("GET");
                    conn.setConnectTimeout(3000);

                    if (conn.getResponseCode() == 200) {
                        try (BufferedReader reader = new BufferedReader(new InputStreamReader(conn.getInputStream()))) {
                            StringBuilder sb = new StringBuilder();
                            String line;
                            while ((line = reader.readLine()) != null) {
                                sb.append(line);
                            }
                            sender.sendMessage(ChatColor.GOLD + "=== [屋顶跑酷排行榜 TOP 5] ===");
                            sender.sendMessage(ChatColor.GRAY + sb.toString());
                        }
                    } else {
                        sender.sendMessage(ChatColor.RED + "排行榜获取失败: HTTP " + conn.getResponseCode());
                    }
                    conn.disconnect();
                } catch (Exception e) {
                    sender.sendMessage(ChatColor.RED + "请求排行榜异常: " + e.getMessage());
                }
            });
            return true;
        }

        sender.sendMessage(ChatColor.GRAY + "用法: /parkour top 查看全服跑酷排行榜");
        return true;
    }
}
