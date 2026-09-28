package api

import (
	"fmt"
	"net/http"
	"strings"
	"time"

	"github.com/gin-gonic/gin"
)

func ValidateStatisticsDateRange() gin.HandlerFunc {
	return func(c *gin.Context) {
		if !strings.HasPrefix(c.Request.URL.Path, "/api/statistics/") {
			c.Next()
			return
		}
		query := c.Request.URL.Query()
		start, end := query.Get("startDate"), query.Get("endDate")
		var dates [2]time.Time
		for index, value := range []string{start, end} {
			if value == "" {
				continue
			}
			date, err := time.ParseInLocation("2006-01-02", value, time.Local)
			if err != nil {
				c.AbortWithStatusJSON(http.StatusBadRequest, gin.H{"code": 400, "message": "日期格式应为 yyyy-MM-dd"})
				return
			}
			dates[index] = date
		}
		if start != "" && end != "" {
			if dates[0].After(dates[1]) {
				dates[0], dates[1] = dates[1], dates[0]
				start, end = end, start
			}
			if dates[1].Sub(dates[0]) > 366*24*time.Hour {
				c.AbortWithStatusJSON(http.StatusBadRequest, gin.H{"code": 400, "message": "查询时间跨度不能超过一年"})
				return
			}
			query.Set("startDate", start)
			query.Set("endDate", end)
			c.Request.URL.RawQuery = query.Encode()
		}
		c.Next()
	}
}

// The home chart needs daily totals, not permission to read production details.
func (h *StatisticsHandler) GetProductionTrend(c *gin.Context) {
	start, end := effectiveStatsDateRange(c.Query("startDate"), c.Query("endDate"))
	id, ids := h.scopeDeviceFilter(c, "", nil)
	query := h.buildProductionStatsBaseQuery(start, end, id, ids)
	var rows []struct {
		Date   string
		Pieces int64
	}
	expr := h.localDateExpr("pr.record_date")
	if err := query.Select(fmt.Sprintf("%s AS date, COALESCE(SUM(pr.pieces),0) AS pieces", expr)).Group(expr).Order("date ASC").Scan(&rows).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"code": 500, "message": "查询产量失败"})
		return
	}
	first, _ := time.ParseInLocation("2006-01-02", start, time.Local)
	last, _ := time.ParseInLocation("2006-01-02", end, time.Local)
	values := map[string]int64{}
	for _, row := range rows {
		values[row.Date] = row.Pieces
	}
	trend := []gin.H{}
	if !first.IsZero() && !last.IsZero() && last.Sub(first) <= 366*24*time.Hour {
		for day := first; !day.After(last); day = day.AddDate(0, 0, 1) {
			key := day.Format("2006-01-02")
			trend = append(trend, gin.H{"date": key, "pieces": values[key]})
		}
	}
	c.JSON(http.StatusOK, gin.H{"code": 0, "data": gin.H{"productionTrend": trend}})
}
