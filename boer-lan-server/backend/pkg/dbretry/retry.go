// Package dbretry retries complete, idempotent operations on transient SQLite locks.
package dbretry

import (
	"context"
	"errors"
	"time"

	"github.com/mattn/go-sqlite3"
)

func IsBusy(err error) bool {
	var sqliteError sqlite3.Error
	return errors.As(err, &sqliteError) && (sqliteError.Code == sqlite3.ErrBusy || sqliteError.Code == sqlite3.ErrLocked)
}

// Do retries only lock conflicts. Callers must roll back failed transactions
// before returning, and must not include network or other external side effects.
func Do(ctx context.Context, operation func() error) error {
	for attempt := 0; ; attempt++ {
		if err := ctx.Err(); err != nil {
			return err
		}
		err := operation()
		if err == nil || !IsBusy(err) || attempt >= 3 {
			return err
		}
		timer := time.NewTimer(time.Duration(attempt+1) * 100 * time.Millisecond)
		select {
		case <-ctx.Done():
			timer.Stop()
			return ctx.Err()
		case <-timer.C:
		}
	}
}
