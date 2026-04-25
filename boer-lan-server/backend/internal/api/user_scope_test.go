package api

import (
	"testing"

	"boer-lan-server/internal/model"
)

func TestResolveUserGroupScopeAlwaysAllowsUnifiedDataView(t *testing.T) {
	tests := []struct {
		name string
		user model.User
		role string
	}{
		{
			name: "admin with explicit groups",
			user: model.User{
				Role:     "admin",
				GroupID:  uintPtr(2),
				GroupIDs: "[2,3]",
			},
			role: "admin",
		},
		{
			name: "non-admin with single group",
			user: model.User{
				Role:     "viewer",
				GroupID:  uintPtr(5),
				GroupIDs: "[5]",
			},
			role: "viewer",
		},
		{
			name: "non-admin with multiple legacy groups",
			user: model.User{
				Role:     "operator",
				GroupID:  uintPtr(7),
				GroupIDs: "[7,8,9]",
			},
			role: "operator",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			scope := resolveUserGroupScope(tt.user, tt.role)
			if !scope.All {
				t.Fatalf("expected unified data view, got restricted scope: %+v", scope)
			}
			if len(scope.GroupIDs) != 0 {
				t.Fatalf("expected no data-filter groups, got %v", scope.GroupIDs)
			}
		})
	}
}

func uintPtr(v uint) *uint {
	return &v
}
